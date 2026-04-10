#pragma once
#include <servercore/buffer/RecvBuffer.h>
#include <cstdint>
#include <cstring>
#include <span>

namespace bench {

static constexpr uint32_t kHeaderSize = 4;
static constexpr uint32_t kMaxPacket = 65536;

inline uint16_t ReadLE16(const std::byte* p) {
    uint16_t val;
    std::memcpy(&val, p, 2);
    return val;
}

// Append data to recv_buf and call on_packet for each complete packet.
// Returns false on buffer overflow or invalid packet (caller should disconnect).
inline bool DrainPackets(
    servercore::buffer::RecvBuffer& recv_buf,
    std::span<const std::byte> data,
    auto&& on_packet)  // void(uint16_t msg_id, const std::byte* body, uint32_t body_len)
{
    auto result = recv_buf.Append(data);
    if (!result) return false;

    while (recv_buf.ReadableSize() >= kHeaderSize) {
        auto region = recv_buf.ReadRegion();
        auto* ptr = region.data();

        uint16_t pkt_size = ReadLE16(ptr);
        if (pkt_size < kHeaderSize || pkt_size > kMaxPacket) return false;
        if (recv_buf.ReadableSize() < pkt_size) break;

        uint16_t msg_id = ReadLE16(ptr + 2);
        on_packet(msg_id, ptr + kHeaderSize, pkt_size - kHeaderSize);
        recv_buf.OnRead(pkt_size);
    }
    return true;
}

} // namespace bench
