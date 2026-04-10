#pragma once

#include <serverweb/HttpParser.h>
#include <serverweb/WsFrameParser.h>
#include <serverweb/WsFrame.h>
#include <serverweb/WebSocketHandler.h>

#include <servercore/buffer/RecvBuffer.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/io/Session.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace serverweb {

class Router;

class HttpSession : public servercore::io::Session {
public:
    HttpSession(int fd, servercore::ring::IoRing& ring,
                servercore::buffer::BufferPool& pool,
                const Router& router);

    void SendResponse(servercore::buffer::SendBufferRef buf);

    // WebSocket API (valid after upgrade)
    void UpgradeToWebSocket(std::shared_ptr<ws::WebSocketHandler> handler);
    void SendText(std::string_view message);
    void SendBinary(std::span<const std::uint8_t> data);
    void SendPing(std::span<const std::uint8_t> payload = {});
    void WsClose(std::uint16_t code = 1000, std::string_view reason = "");
    bool IsWebSocket() const { return mode_ == Mode::WebSocket; }

protected:
    void OnRecv(std::span<const std::byte> data) final;

private:
    enum class Mode { Http, WebSocket };

    void HandleHttpRecv(const std::byte* data, std::uint32_t len);
    void HandleWsRecv(const std::byte* data, std::uint32_t len);
    bool HandleRequest(HttpRequest& request);
    servercore::buffer::SendBufferRef BuildWsFrame(
        ws::WsOpcode opcode, std::span<const std::uint8_t> payload);

    const Router& router_;
    HttpParser parser_;
    servercore::buffer::RecvBuffer recv_buf_;
    std::chrono::steady_clock::time_point last_activity_;

    // WebSocket state
    Mode mode_ = Mode::Http;
    ws::WsFrameParser ws_parser_;
    std::shared_ptr<ws::WebSocketHandler> ws_handler_;
};

} // namespace serverweb
