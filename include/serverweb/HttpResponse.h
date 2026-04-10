#pragma once

#include <serverweb/HttpStatus.h>

#include <servercore/buffer/SendBuffer.h>

#include <string>
#include <string_view>
#include <vector>

namespace serverweb {

class HttpSession;

// Fluent HTTP response builder.
// Two usage patterns:
//   1. Build(pool) -> returns SendBuffer, caller sends manually (legacy)
//   2. Session-bound: constructed with session+pool, call Send() directly
class HttpResponse {
public:
    // Unbound constructor (for Build() pattern)
    HttpResponse() = default;

    // Session-bound constructor (for Send() pattern)
    HttpResponse(HttpSession& session, servercore::buffer::BufferPool& pool);

    HttpResponse& Status(HttpStatus status) {
        status_ = status;
        return *this;
    }

    HttpResponse& Header(std::string name, std::string value) {
        headers_.push_back({std::move(name), std::move(value)});
        return *this;
    }

    HttpResponse& ContentType(std::string_view type) {
        content_type_ = type;
        return *this;
    }

    HttpResponse& Body(std::string body) {
        body_ = std::move(body);
        return *this;
    }

    // Convenience: JSON response (sets Content-Type + body)
    HttpResponse& Json(std::string json_body) {
        content_type_ = "application/json";
        body_ = std::move(json_body);
        return *this;
    }

    HttpResponse& KeepAlive(bool keep) {
        keep_alive_ = keep;
        return *this;
    }

    // Serialize the response and allocate a SendBuffer.
    servercore::buffer::SendBufferRef Build(servercore::buffer::BufferPool& pool) const;

    // Send the response via the bound session.
    void Send();

    // 204 No Content shortcut
    static servercore::buffer::SendBufferRef NoContent(
        servercore::buffer::BufferPool& pool, bool keep_alive = true);

    // Getters
    HttpStatus StatusCode() const { return status_; }
    const std::string& GetBody() const { return body_; }
    bool IsSent() const { return sent_; }
    bool GetKeepAlive() const { return keep_alive_; }

    // Setter for middleware (e.g., compression)
    void SetBody(std::string body) { body_ = std::move(body); }

    void MarkSent() { sent_ = true; }

private:
    struct HeaderPair {
        std::string name;
        std::string value;
    };

    HttpStatus status_ = HttpStatus::kOk;
    std::vector<HeaderPair> headers_;
    std::string content_type_;
    std::string body_;
    bool keep_alive_ = true;
    bool sent_ = false;

    // Session-bound fields (nullable)
    HttpSession* session_ = nullptr;
    servercore::buffer::BufferPool* pool_ = nullptr;
};

} // namespace serverweb
