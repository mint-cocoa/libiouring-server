#pragma once

// Proto.h — Standalone packet serialization for game_bench
// Same wire format as ServerCore's PacketHeader, but no SendBuffer dependency.

#include "Enum.pb.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace bench {

struct PacketHeader {
    uint16_t size;  // Total size including header
    uint16_t id;    // Packet type ID (MsgId)
};
static_assert(sizeof(PacketHeader) == 4);

static constexpr size_t kHeaderSize = sizeof(PacketHeader);

// Serialize a protobuf message into a vector<uint8_t> with PacketHeader prefix.
template<typename T>
std::vector<uint8_t> MakePacket(game::MsgId id, const T& msg) {
    auto body_size = msg.ByteSizeLong();
    auto total = static_cast<uint16_t>(kHeaderSize + body_size);

    std::vector<uint8_t> buf(total);
    PacketHeader hdr{total, static_cast<uint16_t>(id)};
    std::memcpy(buf.data(), &hdr, kHeaderSize);
    msg.SerializeToArray(buf.data() + kHeaderSize, static_cast<int>(body_size));
    return buf;
}

// Parse a protobuf message from raw body bytes (after header).
template<typename T>
bool ParsePacket(const uint8_t* body, size_t body_len, T& msg) {
    return msg.ParseFromArray(body, static_cast<int>(body_len));
}

} // namespace bench
