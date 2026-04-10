#pragma once

#include <cstdint>

namespace serverweb {

enum class WebError : uint8_t {
    kBadRequest,
    kUnauthorized,
    kNotFound,
    kMethodNotAllowed,
    kInternalError,
    kUpgradeRequired,
};

} // namespace serverweb
