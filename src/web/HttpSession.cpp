#include <serverweb/HttpSession.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/Router.h>

#include <spdlog/spdlog.h>

#include <cstring>

namespace serverweb {

HttpSession::HttpSession(int fd, servercore::ring::IoRing& ring,
                         servercore::buffer::BufferPool& pool,
                         const Router& router)
    : Session(fd, ring, pool)
    , router_(router) {
    parser_.SetOnRequest([this](HttpRequest& req) -> bool {
        return HandleRequest(req);
    });
}

void HttpSession::SendResponse(servercore::buffer::SendBufferRef buf) {
    Send(std::move(buf));
}

void HttpSession::OnRecv(std::span<const std::byte> data) {
    last_activity_ = std::chrono::steady_clock::now();

    auto* ptr = data.data();
    auto len = static_cast<std::uint32_t>(data.size());

    if (mode_ == Mode::Http)
        HandleHttpRecv(ptr, len);
    else
        HandleWsRecv(ptr, len);
}

void HttpSession::HandleHttpRecv(const std::byte* data, std::uint32_t len) {
    if (recv_buf_.IsEmpty()) {
        auto* chars = reinterpret_cast<const char*>(data);
        std::uint32_t consumed = parser_.Feed(chars, len);

        if (parser_.HasError()) {
            auto buf = HttpResponse()
                .Status(HttpStatus::kBadRequest)
                .ContentType("text/plain")
                .Body("Bad Request")
                .KeepAlive(false)
                .Build(Pool());
            if (buf) Send(std::move(buf));
            Disconnect();
            return;
        }

        if (consumed < len) {
            if (!recv_buf_.Append(std::span<const std::byte>(data + consumed, len - consumed)).has_value()) {
                spdlog::error("HttpSession[fd={}]: recv buffer overflow", Fd());
                Disconnect();
                return;
            }
        }
    } else {
        if (!recv_buf_.Append(std::span<const std::byte>(data, len)).has_value()) {
            spdlog::error("HttpSession[fd={}]: recv buffer overflow", Fd());
            Disconnect();
            return;
        }

        auto region = recv_buf_.ReadRegion();
        auto* chars = reinterpret_cast<const char*>(region.data());
        auto region_len = static_cast<std::uint32_t>(region.size());

        std::uint32_t consumed = parser_.Feed(chars, region_len);

        if (parser_.HasError()) {
            auto buf = HttpResponse()
                .Status(HttpStatus::kBadRequest)
                .ContentType("text/plain")
                .Body("Bad Request")
                .KeepAlive(false)
                .Build(Pool());
            if (buf) Send(std::move(buf));
            Disconnect();
            return;
        }

        recv_buf_.OnRead(consumed);
        if (recv_buf_.ShouldCompact())
            recv_buf_.Compact();
    }
}

void HttpSession::HandleWsRecv(const std::byte* data, std::uint32_t len) {
    auto* chars = reinterpret_cast<const char*>(data);

    if (recv_buf_.IsEmpty()) {
        ws_parser_.Feed(chars, len);
    } else {
        if (!recv_buf_.Append(std::span<const std::byte>(data, len)).has_value()) {
            Disconnect();
            return;
        }
        auto region = recv_buf_.ReadRegion();
        auto consumed = ws_parser_.Feed(
            reinterpret_cast<const char*>(region.data()), region.size());
        recv_buf_.OnRead(static_cast<std::uint32_t>(consumed));
        if (recv_buf_.ShouldCompact()) recv_buf_.Compact();
    }

    if (ws_parser_.HasError()) Disconnect();
}

bool HttpSession::HandleRequest(HttpRequest& request) {
    HttpResponse response(*this, Pool());
    response.KeepAlive(request.keep_alive);
    RequestContext ctx{*this, request, response, Pool()};
    router_.Dispatch(ctx);

    if (!response.IsSent()) response.Send();

    if (!request.keep_alive) {
        Disconnect();
        return false;
    }

    return true;
}

void HttpSession::UpgradeToWebSocket(std::shared_ptr<ws::WebSocketHandler> handler) {
    mode_ = Mode::WebSocket;
    ws_handler_ = std::move(handler);

    ws_parser_.SetOnFrame([this](ws::WsOpcode opcode,
                                  std::vector<std::uint8_t> payload) {
        switch (opcode) {
        case ws::WsOpcode::kText:
        case ws::WsOpcode::kBinary: {
            bool is_text = (opcode == ws::WsOpcode::kText);
            std::string_view sv(reinterpret_cast<const char*>(payload.data()),
                                 payload.size());
            ws_handler_->OnMessage(*this, sv, is_text);
            break;
        }
        case ws::WsOpcode::kPing: {
            auto buf = BuildWsFrame(ws::WsOpcode::kPong,
                std::span<const std::uint8_t>(payload));
            if (buf) Send(std::move(buf));
            break;
        }
        case ws::WsOpcode::kPong:
            break;  // ignore
        case ws::WsOpcode::kClose: {
            std::uint16_t code = 1000;
            std::string_view reason;
            if (payload.size() >= 2) {
                code = (static_cast<std::uint16_t>(payload[0]) << 8) | payload[1];
                if (payload.size() > 2)
                    reason = std::string_view(
                        reinterpret_cast<const char*>(payload.data() + 2),
                        payload.size() - 2);
            }
            ws_handler_->OnClose(*this, code, reason);
            auto buf = BuildWsFrame(ws::WsOpcode::kClose,
                std::span<const std::uint8_t>(payload));
            if (buf) Send(std::move(buf));
            Disconnect();
            break;
        }
        default:
            break;
        }
    });

    ws_handler_->OnOpen(*this);
}

servercore::buffer::SendBufferRef HttpSession::BuildWsFrame(
    ws::WsOpcode opcode, std::span<const std::uint8_t> payload) {
    std::size_t header_size = 2;
    if (payload.size() >= 126 && payload.size() <= 65535)
        header_size += 2;
    else if (payload.size() > 65535)
        header_size += 8;

    auto total = static_cast<std::uint32_t>(header_size + payload.size());
    auto result = Pool().Allocate(total);
    if (!result) return nullptr;
    auto buf = std::move(*result);

    auto writable = buf->Writable();
    auto* p = reinterpret_cast<std::uint8_t*>(writable.data());

    p[0] = 0x80 | static_cast<std::uint8_t>(opcode);

    if (payload.size() < 126) {
        p[1] = static_cast<std::uint8_t>(payload.size());
    } else if (payload.size() <= 65535) {
        p[1] = 126;
        p[2] = static_cast<std::uint8_t>((payload.size() >> 8) & 0xFF);
        p[3] = static_cast<std::uint8_t>(payload.size() & 0xFF);
    } else {
        p[1] = 127;
        for (int i = 0; i < 8; ++i)
            p[2 + i] = static_cast<std::uint8_t>(
                (payload.size() >> (56 - 8 * i)) & 0xFF);
    }

    if (!payload.empty())
        std::memcpy(p + header_size, payload.data(), payload.size());

    buf->Commit(total);
    return buf;
}

void HttpSession::SendText(std::string_view message) {
    auto payload = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    auto buf = BuildWsFrame(ws::WsOpcode::kText, payload);
    if (buf) Send(std::move(buf));
}

void HttpSession::SendBinary(std::span<const std::uint8_t> data) {
    auto buf = BuildWsFrame(ws::WsOpcode::kBinary, data);
    if (buf) Send(std::move(buf));
}

void HttpSession::SendPing(std::span<const std::uint8_t> payload) {
    auto buf = BuildWsFrame(ws::WsOpcode::kPing, payload);
    if (buf) Send(std::move(buf));
}

void HttpSession::WsClose(std::uint16_t code, std::string_view reason) {
    std::vector<std::uint8_t> payload;
    payload.push_back(static_cast<std::uint8_t>((code >> 8) & 0xFF));
    payload.push_back(static_cast<std::uint8_t>(code & 0xFF));
    payload.insert(payload.end(), reason.begin(), reason.end());
    auto buf = BuildWsFrame(ws::WsOpcode::kClose,
                             std::span<const std::uint8_t>(payload));
    if (buf) Send(std::move(buf));
}

} // namespace serverweb
