#pragma once
#include <servercore/buffer/SendBuffer.h>
#include <string>
#include <string_view>

namespace serverweb {
class HttpRequest;
}

namespace serverweb::ws {

class WsHandshake {
public:
    static bool ValidateUpgrade(const HttpRequest& request);
    static std::string ComputeAcceptKey(std::string_view client_key);
    static servercore::buffer::SendBufferRef BuildUpgradeResponse(
        std::string_view accept_key, servercore::buffer::BufferPool& pool);
};

} // namespace serverweb::ws
