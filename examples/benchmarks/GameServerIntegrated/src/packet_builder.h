#pragma once

#include <servercore/buffer/SendBuffer.h>

#include <cstdint>
#include <cstring>

namespace bench {

template<typename T>
servercore::buffer::SendBufferRef BuildPacket(
    servercore::buffer::BufferPool& pool, uint16_t msg_id, const T& proto)
{
    auto body_size = proto.ByteSizeLong();
    auto total = static_cast<uint16_t>(4 + body_size);
    auto alloc = pool.Allocate(total);
    if (!alloc) return nullptr;
    auto buf = std::move(*alloc);
    auto writable = buf->Writable();
    uint16_t size_le = total;
    uint16_t id_le = msg_id;
    std::memcpy(writable.data(), &size_le, 2);
    std::memcpy(writable.data() + 2, &id_le, 2);
    proto.SerializeToArray(writable.data() + 4, static_cast<int>(body_size));
    buf->Commit(total);
    return buf;
}

} // namespace bench
