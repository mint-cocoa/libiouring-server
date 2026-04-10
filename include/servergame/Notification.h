#pragma once

#include <servercore/Types.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace servergame::notification {

using servercore::PlayerId;

struct Notification {
    uint64_t id = 0;            // DB-assigned (BIGSERIAL)
    PlayerId recipient_id = 0;
    PlayerId sender_id = 0;     // 0 = system notification
    int32_t code = 0;
    std::string subject;
    std::string content;        // JSON
    bool persistent = true;     // false = don't save to DB
    std::chrono::system_clock::time_point created_at;
};

} // namespace servergame::notification
