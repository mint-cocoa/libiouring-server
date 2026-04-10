#pragma once

#include <servercore/buffer/SendBuffer.h>
#include <servercore/Concepts.h>
#include "../types.h"
#include <spdlog/spdlog.h>
#include <cstring>

struct PacketBuilder {
    template<servercore::ProtobufMessage T>
    static servercore::buffer::SendBufferRef Build(
        servercore::buffer::BufferPool& pool,
        MsgId msg_id,
        const T& proto)
    {
        auto payload_size = static_cast<std::uint32_t>(proto.ByteSizeLong());
        std::uint32_t total = 4 + payload_size;

        auto result = pool.Allocate(total);
        if (!result) {
            spdlog::error("PacketBuilder: failed to allocate buffer, size={}", total);
            return nullptr;
        }

        auto buf = std::move(*result);
        auto writable = buf->Writable();
        auto* w = writable.data();

        auto size_val = static_cast<std::uint16_t>(total);
        auto id_val   = static_cast<std::uint16_t>(msg_id);
        std::memcpy(w,     &size_val, 2);
        std::memcpy(w + 2, &id_val,   2);
        proto.SerializeToArray(w + 4, static_cast<int>(payload_size));

        buf->Commit(total);
        return buf;
    }
};
