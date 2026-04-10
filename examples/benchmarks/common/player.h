#pragma once
#include "types.h"
#include <servercore/io/Session.h>
#include <servercore/ring/IoRing.h>
#include <memory>
#include <string>

namespace bench {

struct PlayerState {
    PlayerId player_id = 0;
    std::string name;
    float pos_x = 0, pos_y = 0, pos_z = 0;
    float rotation_y = 0;
    float vel_x = 0, vel_y = 0, vel_z = 0;
    int32_t move_state = 0;  // 0=idle, 1=walk, 2=run
    int32_t hp = 100, max_hp = 100;
    bool is_dead = false;
    TimePoint death_time;

    // IO ownership tracking (io_uring)
    servercore::ring::IoRing* owner_ring = nullptr;
    std::weak_ptr<servercore::io::Session> session;
    servercore::SessionId session_id = 0;
    WorkerId worker_id = 0;
    ZoneId zone_id = 0;
};

} // namespace bench
