#pragma once

#include <servercore/buffer/SendBuffer.h>

#include <string_view>

namespace servergame {

/// Build a WebSocket text frame (FIN, unmasked, server->client).
servercore::buffer::SendBufferRef BuildWsTextFrame(
    servercore::buffer::BufferPool& pool, std::string_view text);

} // namespace servergame
