#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace bench {

template<typename T>
std::vector<uint8_t> BuildPacket(uint16_t msg_id, const T& proto) {
    auto body_size = proto.ByteSizeLong();
    auto total = static_cast<uint16_t>(4 + body_size);
    std::vector<uint8_t> buf(total);
    std::memcpy(buf.data(), &total, 2);
    std::memcpy(buf.data() + 2, &msg_id, 2);
    proto.SerializeToArray(buf.data() + 4, static_cast<int>(body_size));
    return buf;
}

} // namespace bench
