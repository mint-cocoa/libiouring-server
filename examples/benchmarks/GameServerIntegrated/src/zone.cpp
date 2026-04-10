#include "zone.h"
#include "packet_builder.h"

#include "Enum.pb.h"
#include "Game.pb.h"
#include "Common.pb.h"

#include <servercore/ring/IoRing.h>
#include <servercore/io/Session.h>

#include <chrono>
#include <vector>

namespace bench {

// Lightweight struct for sending outside the lock.
struct SendTarget {
    std::weak_ptr<servercore::io::Session> session;
    servercore::ring::IoRing* owner_ring;
};

static void DispatchSend(const SendTarget& target, servercore::buffer::SendBufferRef buf) {
    auto sess = target.session.lock();
    if (!sess) return;

    auto* current_ring = servercore::ring::IoRing::Current();
    if (target.owner_ring == current_ring) {
        sess->Send(buf);
    } else if (target.owner_ring) {
        std::weak_ptr<servercore::io::Session> weak = target.session;
        auto buf_copy = buf;
        target.owner_ring->RunOnRing([weak = std::move(weak), buf = std::move(buf_copy)]() {
            if (auto s = weak.lock()) {
                s->Send(buf);
            }
        });
    }
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

    // Snapshot targets under lock, then send outside
    std::vector<SendTarget> targets;
    {
        std::lock_guard lock(mutex_);
        targets = SnapshotTargets();
        players_[pid] = std::move(state);
    }

    if (!targets.empty()) {
        auto buf = BuildPacket(pool, game::S_SPAWN, spawn);
        if (buf) {
            for (auto& t : targets) {
                DispatchSend(t, buf);
            }
        }
    }
}

void Zone::RemovePlayer(PlayerId pid, servercore::buffer::BufferPool& pool) {
    std::vector<SendTarget> targets;
    {
        std::lock_guard lock(mutex_);
        auto it = players_.find(pid);
        if (it == players_.end()) return;
        players_.erase(it);
        targets = SnapshotTargets();
    }

    // Broadcast S_DESPAWN to remaining players
    if (!targets.empty()) {
        game::S_Despawn despawn;
        despawn.set_player_id(pid);
        auto buf = BuildPacket(pool, game::S_DESPAWN, despawn);
        if (buf) {
            for (auto& t : targets) {
                DispatchSend(t, buf);
            }
        }
    }
}

PlayerState* Zone::FindPlayer(PlayerId pid) {
    std::lock_guard lock(mutex_);
    auto it = players_.find(pid);
    return it != players_.end() ? &it->second : nullptr;
}

void Zone::BroadcastToAll(servercore::buffer::SendBufferRef buf, PlayerId exclude) {
    std::vector<SendTarget> targets;
    {
        std::lock_guard lock(mutex_);
        targets = SnapshotTargets(exclude);
    }
    for (auto& t : targets) {
        DispatchSend(t, buf);
    }
}

void Zone::SendToPlayer(PlayerId pid, servercore::buffer::SendBufferRef buf) {
    SendTarget target;
    {
        std::lock_guard lock(mutex_);
        auto it = players_.find(pid);
        if (it == players_.end()) return;
        target.session = it->second.session;
        target.owner_ring = it->second.owner_ring;
    }
    DispatchSend(target, buf);
}

void Zone::DoSend(const PlayerState& ps, servercore::buffer::SendBufferRef buf) {
    auto sess = ps.session.lock();
    if (!sess) return;

    auto* current_ring = servercore::ring::IoRing::Current();
    if (ps.owner_ring == current_ring) {
        // Same thread -- direct send
        sess->Send(buf);
    } else if (ps.owner_ring) {
        // Cross-thread -- post to target ring
        std::weak_ptr<servercore::io::Session> weak = ps.session;
        auto buf_copy = buf;
        ps.owner_ring->RunOnRing([weak = std::move(weak), buf = std::move(buf_copy)]() {
            if (auto s = weak.lock()) {
                s->Send(buf);
            }
        });
    }
}

std::vector<SendTarget> Zone::SnapshotTargets(PlayerId exclude) const {
    std::vector<SendTarget> targets;
    targets.reserve(players_.size());
    for (auto& [pid, ps] : players_) {
        if (pid == exclude) continue;
        targets.push_back({ps.session, ps.owner_ring});
    }
    return targets;
}

void Zone::Tick(servercore::buffer::BufferPool& pool) {
    std::lock_guard lock(mutex_);
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
