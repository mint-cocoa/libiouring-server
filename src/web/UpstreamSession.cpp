#include <serverweb/UpstreamSession.h>

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <charconv>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <liburing.h>

namespace serverweb {

UpstreamSession::UpstreamSession(servercore::ring::IoRing& ring,
                                 servercore::buffer::BufferPool& pool)
    : ring_(ring)
    , pool_(pool) {}

UpstreamSession::~UpstreamSession() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void UpstreamSession::Init() {
    auto self = std::static_pointer_cast<UpstreamSession>(shared_from_this());
    connect_ev_.SetOwner(self);
    send_ev_.SetOwner(self);
    recv_ev_.SetOwner(self);
}

void UpstreamSession::Connect(const std::string& host, std::uint16_t port,
                               std::string request_bytes,
                               ProxyCallback on_response,
                               ProxyErrorCallback on_error) {
    pending_request_ = std::move(request_bytes);
    on_response_ = std::move(on_response);
    on_error_ = std::move(on_error);
    recv_buffer_.clear();

    // Keep alive during async operations
    self_ref_ = std::static_pointer_cast<UpstreamSession>(shared_from_this());

    // Synchronous DNS resolution
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto port_str = std::to_string(port);
    struct addrinfo* result = nullptr;
    int ret = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0 || !result) {
        std::string err = "getaddrinfo failed for " + host + ":" + port_str +
                          ": " + ::gai_strerror(ret);
        spdlog::error("UpstreamSession: {}", err);
        if (on_error_) on_error_(std::move(err));
        self_ref_.reset();
        return;
    }

    // Copy resolved address
    std::memcpy(&connect_addr_, result->ai_addr, result->ai_addrlen);
    connect_addr_len_ = result->ai_addrlen;

    // Create non-blocking socket
    fd_ = ::socket(result->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    ::freeaddrinfo(result);

    if (fd_ < 0) {
        std::string err = "socket() failed: " + std::string(std::strerror(errno));
        spdlog::error("UpstreamSession: {}", err);
        if (on_error_) on_error_(std::move(err));
        self_ref_.reset();
        return;
    }

    // Submit connect via io_uring
    if (!ring_.PrepConnect(connect_ev_, fd_,
                           reinterpret_cast<const struct sockaddr*>(&connect_addr_),
                           connect_addr_len_)) {
        std::string err = "PrepConnect failed: SQE ring full";
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }
    ring_.Submit();
}

void UpstreamSession::OnConnect(servercore::ring::ConnectEvent& /*ev*/,
                                 std::int32_t result) {
    if (result < 0) {
        std::string err = "connect failed: " + std::string(std::strerror(-result));
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }

    connected_ = true;
    spdlog::debug("UpstreamSession[fd={}]: connected to upstream", fd_);
    SendRequest();
}

void UpstreamSession::SendRequest() {
    // Allocate a SendBuffer and copy the request into it
    auto buf_result = pool_.Allocate(static_cast<std::uint32_t>(pending_request_.size()));
    if (!buf_result) {
        std::string err = "failed to allocate send buffer";
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }

    send_buf_ = std::move(*buf_result);
    auto writable = send_buf_->Writable();
    std::memcpy(writable.data(), pending_request_.data(), pending_request_.size());
    send_buf_->Commit(static_cast<std::uint32_t>(pending_request_.size()));
    pending_request_.clear();

    // Set up msghdr + iovec pointing to the SendBuffer's Data()
    auto data = send_buf_->Data();
    send_iov_.iov_base = const_cast<std::byte*>(data.data());
    send_iov_.iov_len = data.size();

    std::memset(&send_msg_, 0, sizeof(send_msg_));
    send_msg_.msg_iov = &send_iov_;
    send_msg_.msg_iovlen = 1;

    send_ev_.SetRequestedBytes(data.size());

    if (!ring_.PrepSendMsg(send_ev_, fd_, &send_msg_, MSG_NOSIGNAL)) {
        std::string err = "PrepSendMsg failed: SQE ring full";
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }
    ring_.Submit();
}

void UpstreamSession::OnSend(servercore::ring::SendEvent& /*ev*/,
                               std::int32_t result) {
    // Release send buffer
    send_buf_.reset();

    if (result < 0) {
        std::string err = "send failed: " + std::string(std::strerror(-result));
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }

    spdlog::debug("UpstreamSession[fd={}]: sent {} bytes to upstream", fd_, result);
    StartRecv();
}

void UpstreamSession::StartRecv() {
    if (!ring_.PrepRecv(recv_ev_, fd_)) {
        std::string err = "PrepRecv failed: SQE ring full";
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }
    ring_.Submit();
}

void UpstreamSession::OnRecv(servercore::ring::RecvEvent& /*ev*/,
                               std::int32_t result, std::uint32_t flags) {
    bool has_buffer = (flags & IORING_CQE_F_BUFFER) != 0;
    std::uint16_t buf_id = 0;

    if (has_buffer) {
        buf_id = static_cast<std::uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
        auto& buf_ring = ring_.BufRing();

        if (result > 0) {
            auto view = buf_ring.View(buf_id, static_cast<std::uint32_t>(result));
            recv_buffer_.insert(recv_buffer_.end(),
                reinterpret_cast<const std::byte*>(view.data()),
                reinterpret_cast<const std::byte*>(view.data()) + view.size());
        }

        buf_ring.Return(buf_id);
    }

    if (result == 0) {
        // Connection closed by upstream — treat accumulated data as complete response
        spdlog::debug("UpstreamSession[fd={}]: upstream closed connection", fd_);
        ParseResponse();
        Close();
        return;
    }

    if (result < 0) {
        std::string err = "recv failed: " + std::string(std::strerror(-result));
        spdlog::error("UpstreamSession[fd={}]: {}", fd_, err);
        if (on_error_) on_error_(std::move(err));
        Close();
        return;
    }

    // Check if we have a complete HTTP response (Content-Length based)
    // Look for end of headers
    auto data_view = std::string_view(
        reinterpret_cast<const char*>(recv_buffer_.data()),
        recv_buffer_.size());

    auto header_end = data_view.find("\r\n\r\n");
    if (header_end != std::string_view::npos) {
        std::size_t body_start = header_end + 4;

        // Find Content-Length header
        auto cl_pos = data_view.find("Content-Length:");
        if (cl_pos == std::string_view::npos)
            cl_pos = data_view.find("content-length:");

        if (cl_pos != std::string_view::npos && cl_pos < header_end) {
            auto val_start = cl_pos + 15; // len("Content-Length:")
            while (val_start < header_end && data_view[val_start] == ' ')
                ++val_start;
            auto val_end = data_view.find("\r\n", val_start);
            if (val_end == std::string_view::npos || val_end > header_end)
                val_end = header_end;

            std::size_t content_length = 0;
            auto [ptr, ec] = std::from_chars(
                data_view.data() + val_start,
                data_view.data() + val_end,
                content_length);

            if (ec == std::errc{}) {
                std::size_t expected_total = body_start + content_length;
                if (recv_buffer_.size() >= expected_total) {
                    ParseResponse();
                    Close();
                    return;
                }
            }
        } else {
            // Check for Transfer-Encoding: chunked
            auto te_pos = data_view.find("Transfer-Encoding: chunked");
            if (te_pos == std::string_view::npos)
                te_pos = data_view.find("transfer-encoding: chunked");

            if (te_pos != std::string_view::npos) {
                // Look for chunked terminator: 0\r\n\r\n
                if (data_view.find("\r\n0\r\n\r\n") != std::string_view::npos) {
                    ParseResponse();
                    Close();
                    return;
                }
            }
            // No Content-Length and not chunked — could be a response with no body
            // (e.g. 204, 304, HEAD) or connection-close semantics.
            // For responses like 1xx, 204, 304, we know there's no body.
            // Otherwise, continue reading until connection close.
        }
    }

    // Not complete yet — read more
    StartRecv();
}

void UpstreamSession::ParseResponse() {
    auto data_view = std::string_view(
        reinterpret_cast<const char*>(recv_buffer_.data()),
        recv_buffer_.size());

    // Find end of headers
    auto header_end = data_view.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        if (on_error_) on_error_("malformed response: no header terminator");
        return;
    }

    // Parse status line: HTTP/1.x SSS Reason\r\n
    auto status_line_end = data_view.find("\r\n");
    if (status_line_end == std::string_view::npos) {
        if (on_error_) on_error_("malformed response: no status line");
        return;
    }

    auto status_line = data_view.substr(0, status_line_end);

    // Find the status code (after first space)
    auto sp1 = status_line.find(' ');
    if (sp1 == std::string_view::npos) {
        if (on_error_) on_error_("malformed status line");
        return;
    }

    int status_code = 0;
    auto [ptr, ec] = std::from_chars(
        status_line.data() + sp1 + 1,
        status_line.data() + sp1 + 4,
        status_code);

    if (ec != std::errc{}) {
        if (on_error_) on_error_("malformed status code");
        return;
    }

    // Parse headers
    std::vector<std::pair<std::string, std::string>> headers;
    std::size_t pos = status_line_end + 2; // skip \r\n after status line
    while (pos < header_end) {
        auto line_end = data_view.find("\r\n", pos);
        if (line_end == std::string_view::npos || line_end > header_end)
            break;

        auto line = data_view.substr(pos, line_end - pos);
        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            auto name = line.substr(0, colon);
            auto value = line.substr(colon + 1);
            // Trim leading whitespace from value
            while (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);
            headers.emplace_back(std::string(name), std::string(value));
        }
        pos = line_end + 2;
    }

    // Extract body
    std::size_t body_start = header_end + 4;
    std::vector<std::byte> body;
    if (body_start < recv_buffer_.size()) {
        body.assign(recv_buffer_.begin() + static_cast<std::ptrdiff_t>(body_start),
                    recv_buffer_.end());
    }

    if (on_response_) {
        on_response_(status_code, std::move(headers), std::move(body));
    }
}

void UpstreamSession::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
    pending_request_.clear();
    on_response_ = nullptr;
    on_error_ = nullptr;
    self_ref_.reset();
}

} // namespace serverweb
