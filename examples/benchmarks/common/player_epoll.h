#pragma once
#include "types.h"
#include <string>

namespace bench {

class EpollConnection;  // forward declaration

struct EpollPlayerState {
    PlayerId player_id = 0;
    std::string name;
    float pos_x = 0, pos_y = 0, pos_z = 0;
    float rotation_y = 0;
    float vel_x = 0, vel_y = 0, vel_z = 0;
    int32_t move_state = 0;
    int32_t hp = 100, max_hp = 100;
    bool is_dead = false;
    TimePoint death_time;

    EpollConnection* connection = nullptr;  // same thread, raw pointer
    WorkerId worker_id = 0;
    ZoneId zone_id = 0;
};

} // namespace bench
