#pragma once

#include <cstdint>

namespace serverstorage {

enum class StorageError : uint8_t {
    kConnectionFailed,
    kQueryFailed,
    kNotFound,
    kSerialization,
    kPoolExhausted,
};

} // namespace serverstorage
