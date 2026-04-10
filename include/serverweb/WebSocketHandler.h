#pragma once
#include <cstdint>
#include <string_view>

namespace serverweb {
class HttpSession;
}

namespace serverweb::ws {

class WebSocketHandler {
public:
    virtual ~WebSocketHandler() = default;
    virtual void OnOpen(HttpSession& session) {}
    virtual void OnMessage(HttpSession& session,
                           std::string_view data, bool is_text) = 0;
    virtual void OnClose(HttpSession& session,
                         std::uint16_t code, std::string_view reason) {}
};

} // namespace serverweb::ws
