#include <serverweb/HttpResponse.h>
#include <serverweb/HttpSession.h>

#include <cstring>
#include <ctime>
#include <string>

namespace serverweb {

// RFC 7231 Date header: "Sun, 06 Nov 1994 08:49:37 GMT"
static void AppendDateHeader(std::string& out) {
    out += "Date: ";
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
    out += buf;
    out += "\r\n";
}

HttpResponse::HttpResponse(HttpSession& session, servercore::buffer::BufferPool& pool)
    : session_(&session)
    , pool_(&pool) {}

servercore::buffer::SendBufferRef HttpResponse::Build(servercore::buffer::BufferPool& pool) const {
    std::string response;
    response.reserve(256 + body_.size());

    // Status line
    response += "HTTP/1.1 ";
    response += std::to_string(static_cast<int>(status_));
    response += ' ';
    response += HttpStatusToString(status_);
    response += "\r\n";

    // Server
    response += "Server: serverweb\r\n";

    // Date
    AppendDateHeader(response);

    // Content-Type
    if (!content_type_.empty()) {
        response += "Content-Type: ";
        response += content_type_;
        response += "\r\n";
    }

    // Content-Length
    response += "Content-Length: ";
    response += std::to_string(body_.size());
    response += "\r\n";

    // Connection
    response += "Connection: ";
    response += keep_alive_ ? "keep-alive" : "close";
    response += "\r\n";

    // Custom headers
    for (auto& h : headers_) {
        response += h.name;
        response += ": ";
        response += h.value;
        response += "\r\n";
    }

    // End of headers
    response += "\r\n";

    // Body
    response += body_;

    // Allocate SendBuffer and copy
    auto result = pool.Allocate(static_cast<std::uint32_t>(response.size()));
    if (!result) return nullptr;
    auto buf = std::move(*result);

    auto writable = buf->Writable();
    std::memcpy(writable.data(), response.data(), response.size());
    buf->Commit(static_cast<std::uint32_t>(response.size()));

    return buf;
}

void HttpResponse::Send() {
    if (sent_) return;
    sent_ = true;

    if (!session_ || !pool_) return;

    auto buf = Build(*pool_);
    if (buf) session_->SendResponse(buf);
}

servercore::buffer::SendBufferRef HttpResponse::NoContent(
    servercore::buffer::BufferPool& pool, bool keep_alive) {
    return HttpResponse()
        .Status(HttpStatus::kNoContent)
        .KeepAlive(keep_alive)
        .Build(pool);
}

} // namespace serverweb
