#pragma once

#include <servercore/Types.h>

#include <string>

namespace servergame::presence {

using servercore::PlayerId;

enum class PresenceEventType { kJoin, kLeave, kUpdate };

struct PresenceEvent {
    PresenceEventType type;
    PlayerId player_id;
    std::string status;
    std::string metadata;
};

} // namespace servergame::presence
