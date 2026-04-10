#include "zone.h"
#include "epoll_connection.h"
#include "packet_builder.h"

#include "Enum.pb.h"
#include "Game.pb.h"
#include "Common.pb.h"

#include <chrono>

namespace bench {

void Zone::AddPlayer(PlayerId pid, EpollPlayerState state) {
    // Build S_SPAWN packet before inserting
    game::S_Spawn spawn;
    auto* info = spawn.mutable_player();
    info->set_player_id(state.player_id);
    info->set_name(state.name);
    auto* pos = info->mutable_position();
    pos->set_x(state.pos_x);
    pos->set_y(state.pos_y);
    pos->set_z(state.pos_z);
    info->set_rotation_y(state.rotation_y);
    info->set_hp(state.hp);
    info->set_max_hp(state.max_hp);
    info->set_level(1);
    info->set_zone_id(zone_id_);

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Broadcast S_SPAWN to existing players
    if (!players_.empty()) {
        auto pkt = BuildPacket(game::S_SPAWN, spawn);
        BroadcastToAllLocked(pkt);
    }

    players_[pid] = std::move(state);
}

void Zone::RemovePlayer(PlayerId pid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = players_.find(pid);
    if (it == players_.end()) return;

    players_.erase(it);

    // Broadcast S_DESPAWN to remaining players
    if (!players_.empty()) {
        game::S_Despawn despawn;
        despawn.set_player_id(pid);
        auto pkt = BuildPacket(game::S_DESPAWN, despawn);
        BroadcastToAllLocked(pkt);
    }
}

EpollPlayerState* Zone::FindPlayer(PlayerId pid) {
    auto it = players_.find(pid);
    return it != players_.end() ? &it->second : nullptr;
}

void Zone::BroadcastToAll(const std::vector<uint8_t>& pkt, PlayerId exclude) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    BroadcastToAllLocked(pkt, exclude);
}

void Zone::BroadcastToAllLocked(const std::vector<uint8_t>& pkt, PlayerId exclude) {
    for (auto& [pid, ps] : players_) {
        if (pid == exclude) continue;
        if (ps.connection) {
            ps.connection->QueueSend(pkt.data(), pkt.size());
        }
    }
}

void Zone::SendToPlayer(PlayerId pid, const std::vector<uint8_t>& pkt) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* ps = FindPlayer(pid);
    if (!ps || !ps->connection) return;
    ps->connection->QueueSend(pkt.data(), pkt.size());
}

void Zone::Tick() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Respawn dead players after 3 seconds
    auto now = std::chrono::steady_clock::now();
    for (auto& [pid, ps] : players_) {
        if (ps.is_dead) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - ps.death_time).count();
            if (elapsed >= 3) {
                ps.is_dead = false;
                ps.hp = ps.max_hp;
            }
        }
    }
}

} // namespace bench
