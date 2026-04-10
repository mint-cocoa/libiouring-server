#pragma once

#include <servercore/Types.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace servergame::matchmaker {

using servercore::PlayerId;
using servercore::ContextId;

struct MatchmakerTicket {
    uint64_t ticket_id = 0;
    PlayerId player_id = 0;
    ContextId context_id = 0;
    uint32_t min_count = 2;
    uint32_t max_count = 2;
    std::string query;
    std::string handler_name;
    std::unordered_map<std::string, std::string> string_props;
    std::unordered_map<std::string, double> numeric_props;
    std::chrono::steady_clock::time_point submitted_at;
    std::chrono::seconds timeout{30};
};

} // namespace servergame::matchmaker
