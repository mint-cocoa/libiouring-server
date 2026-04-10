#include "zone.h"
#include "packet_handler.h"

#include "Enum.pb.h"
#include "Game.pb.h"
#include "Common.pb.h"

#include <servercore/ring/IoRing.h>
#include <servercore/io/Session.h>

#include <chrono>

namespace bench {

void Zone::PostPacket(ZonePacket pkt) {
    packet_queue_.Push(std::move(pkt));
}

std::size_t Zone::DrainPacketQueue() {
    return packet_queue_.Drain([this](ZonePacket&& pkt) {
        HandlePacket(pkt.player_id, pkt.msg_id,
                     pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
    });
}

void Zone::HandlePacket(PlayerId player_id, uint16_t msg_id,
                        const std::byte* body, uint32_t body_len) {
    if (!pool_) return;

    // msg_id == 0 is a sentinel for disconnect
    if (msg_id == 0) {
        RemovePlayer(player_id, *pool_);
        return;
    }

    HandleZonePacket(msg_id, body, body_len, *this, *pool_, player_id);
}

void Zone::AddPlayer(PlayerId pid, PlayerState state, servercore::buffer::BufferPool& pool) {
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

    // Broadcast S_SPAWN to existing players
    if (!players_.empty()) {
        auto buf = BuildPacket(pool, game::S_SPAWN, spawn);
        if (buf) {
            BroadcastToAll(buf);
        }
    }

    players_[pid] = std::move(state);
}

void Zone::RemovePlayer(PlayerId pid, servercore::buffer::BufferPool& pool) {
    auto it = players_.find(pid);
    if (it == players_.end()) return;

    players_.erase(it);

    // Broadcast S_DESPAWN to remaining players
    if (!players_.empty()) {
        game::S_Despawn despawn;
        despawn.set_player_id(pid);
        auto buf = BuildPacket(pool, game::S_DESPAWN, despawn);
        if (buf) {
            BroadcastToAll(buf);
        }
    }
}

PlayerState* Zone::FindPlayer(PlayerId pid) {
    auto it = players_.find(pid);
    return it != players_.end() ? &it->second : nullptr;
}

void Zone::BroadcastToAll(servercore::buffer::SendBufferRef buf, PlayerId exclude) {
    for (auto& [pid, ps] : players_) {
        if (pid == exclude) continue;
        DoSend(ps, buf);
    }
}

void Zone::SendToPlayer(PlayerId pid, servercore::buffer::SendBufferRef buf) {
    auto* ps = FindPlayer(pid);
    if (!ps) return;
    DoSend(*ps, buf);
}

void Zone::DoSend(const PlayerState& ps, servercore::buffer::SendBufferRef buf) {
    if (!ps.owner_ring) return;

    // Zone worker thread is NEVER the IO thread -- always use RunOnRing
    std::weak_ptr<servercore::io::Session> weak = ps.session;
    auto buf_copy = buf;
    ps.owner_ring->RunOnRing([weak = std::move(weak), buf = std::move(buf_copy)]() {
        if (auto s = weak.lock()) {
            s->Send(buf);
        }
    });
}

void Zone::Tick(servercore::buffer::BufferPool& /*pool*/) {
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
