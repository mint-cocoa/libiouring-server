#pragma once

// Config.h — CLI configuration for game_bench.

#include <cstdint>

namespace bench {

struct Config {
    const char* host     = "127.0.0.1";
    uint16_t    port     = 7777;
    int         threads  = 4;
    int         conns    = 10;        // per thread
    int         duration = 30;        // seconds
    int         warmup   = 5;         // seconds
    int         move_interval_ms  = 50;   // C_MOVE period (20 Hz)
    int         attack_interval_ms = 500; // C_ATTACK period
    bool        no_attack = false;
    int         ramp_delay_ms = 10;   // delay between connections
    int         rooms    = 0;         // 0=single zone, N>0=split into N rooms
    const char* label    = "";
    const char* csv_file = nullptr;
};

} // namespace bench
