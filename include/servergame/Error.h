#pragma once

#include <cstdint>

namespace servergame {

enum class GameError : uint8_t {
    kPlayerNotFound,
    kRoomFull,
    kMatchNotFound,
    kInvalidAction,
    kAlreadyJoined,
};

} // namespace servergame
