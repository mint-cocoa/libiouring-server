#pragma once

#include <cstdint>
#include <string>

namespace servergame::match {

struct MatchConfig {
    uint32_t tick_rate = 20;     // Hz (50ms per tick at 20Hz)
    uint32_t max_players = 0;    // 0 = unlimited
    std::string handler_name;
    std::string label;
};

} // namespace servergame::match
