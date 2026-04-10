#include <servergame/WsFrameBuilder.h>

#include <cstring>

using namespace servercore;

namespace servergame {

buffer::SendBufferRef BuildWsTextFrame(buffer::BufferPool& pool, std::string_view text) {
    std::uint32_t header_size = 2;
    if (text.size() >= 126 && text.size() <= 65535)
        header_size = 4;
    else if (text.size() > 65535)
        header_size = 10;

    auto total = static_cast<std::uint32_t>(header_size + text.size());
    auto result = pool.Allocate(total);
    if (!result) return nullptr;
    auto buf = std::move(*result);

    auto writable = buf->Writable();
    auto* p = reinterpret_cast<std::uint8_t*>(writable.data());

    p[0] = 0x81; // FIN + Text opcode

    if (text.size() < 126) {
        p[1] = static_cast<std::uint8_t>(text.size());
    } else if (text.size() <= 65535) {
        p[1] = 126;
        p[2] = static_cast<std::uint8_t>((text.size() >> 8) & 0xFF);
        p[3] = static_cast<std::uint8_t>(text.size() & 0xFF);
    } else {
        p[1] = 127;
        for (int i = 0; i < 8; ++i)
            p[2 + i] = static_cast<std::uint8_t>(
                (text.size() >> (56 - 8 * i)) & 0xFF);
    }

    if (!text.empty())
        std::memcpy(p + header_size, text.data(), text.size());

    buf->Commit(total);
    return buf;
}

} // namespace servergame
