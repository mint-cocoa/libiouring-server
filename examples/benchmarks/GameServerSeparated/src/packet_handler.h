#pragma once

#include "types.h"
#include "player.h"
#include "zone.h"
#include "zone_manager.h"
#include "combat.h"
#include "packet_builder.h"

#include "Enum.pb.h"
#include "Auth.pb.h"
#include "Game.pb.h"
#include "Common.pb.h"

#include <servercore/io/Session.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/ring/IoRing.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace bench {

inline std::atomic<uint64_t> g_next_player_id{1};

inline game::PlayerInfo MakePlayerInfo(const PlayerState& ps) {
    game::PlayerInfo info;
    info.set_player_id(ps.player_id);
    info.set_name(ps.name);
    auto* pos = info.mutable_position();
    pos->set_x(ps.pos_x);
    pos->set_y(ps.pos_y);
    pos->set_z(ps.pos_z);
    info.set_rotation_y(ps.rotation_y);
    info.set_hp(ps.hp);
    info.set_max_hp(ps.max_hp);
    info.set_level(1);
    info.set_zone_id(ps.zone_id);
    return info;
}

// ── IO thread packet handler (pre-zone packets only) ──────────────────
// Handles C_LOGIN and C_ENTER_GAME on the IO thread.
// Returns true if the packet was handled here (pre-zone).
// Returns false if the packet should be forwarded to the zone worker.
inline bool HandleIoPacket(
    uint16_t msg_id,
    const std::byte* body,
    uint32_t body_len,
    std::shared_ptr<servercore::io::Session> session_ptr,
    servercore::buffer::BufferPool& pool,
    ZoneManager& zone_mgr,
    PlayerId& player_id,
    ZoneId& zone_id,
    WorkerId worker_id,
    bool& logged_in,
    bool& in_game)
{
    auto& session = *session_ptr;

    switch (msg_id) {
    case game::C_LOGIN: {
        game::C_Login req;
        if (!req.ParseFromArray(body, body_len)) return true;

        player_id = g_next_player_id.fetch_add(1, std::memory_order_relaxed);
        logged_in = true;

        game::S_Login res;
        res.set_success(true);
        res.set_player_id(player_id);

        auto buf = BuildPacket(pool, game::S_LOGIN, res);
        if (buf) session.Send(std::move(buf));
        return true;
    }
    case game::C_ENTER_GAME: {
        if (!logged_in) return true;

        // Default zone (lobby)
        zone_id = 0;
        auto* zone = zone_mgr.GetDefaultZone();
        if (!zone) return true;

        PlayerState ps;
        ps.player_id = player_id;
        ps.name = "Player_" + std::to_string(player_id);
        ps.hp = 100;
        ps.max_hp = 100;
        ps.zone_id = zone_id;
        ps.owner_ring = servercore::ring::IoRing::Current();
        ps.session = session_ptr;
        ps.session_id = session.GetSessionId();
        ps.worker_id = worker_id;

        // Reply S_ENTER_GAME on IO thread (direct send is safe here)
        game::S_EnterGame res;
        res.set_success(true);
        *res.mutable_player() = MakePlayerInfo(ps);
        auto* map_data = res.mutable_map_data();
        map_data->set_grid_width(10);
        map_data->set_grid_height(10);
        map_data->set_cell_size(1.0f);

        auto enter_buf = BuildPacket(pool, game::S_ENTER_GAME, res);
        if (enter_buf) session.Send(enter_buf);

        // Send S_PLAYER_LIST with existing players
        game::S_PlayerList player_list;
        for (auto& [pid, existing] : zone->Players()) {
            *player_list.add_players() = MakePlayerInfo(existing);
        }
        auto list_buf = BuildPacket(pool, game::S_PLAYER_LIST, player_list);
        if (list_buf) session.Send(list_buf);

        // Add to zone (will broadcast S_SPAWN via RunOnRing)
        zone->AddPlayer(player_id, ps, pool);

        in_game = true;
        return true;
    }
    default:
        // Not a pre-zone packet -- forward to zone worker
        return false;
    }
}

// ── Zone worker packet handler ────────────────────────────────────────
// Called on zone worker thread via MpscQueue drain.
inline void HandleZonePacket(
    uint16_t msg_id,
    const std::byte* body,
    uint32_t body_len,
    Zone& zone,
    servercore::buffer::BufferPool& pool,
    PlayerId player_id)
{
    switch (msg_id) {
    case game::C_MOVE: {
        game::C_Move req;
        if (!req.ParseFromArray(body, body_len)) return;

        auto* ps = zone.FindPlayer(player_id);
        if (!ps) return;

        // Update position -- preserve float values exactly (timestamps!)
        if (req.has_position()) {
            ps->pos_x = req.position().x();
            ps->pos_y = req.position().y();
            ps->pos_z = req.position().z();
        }
        ps->rotation_y = req.rotation_y();
        if (req.has_velocity()) {
            ps->vel_x = req.velocity().x();
            ps->vel_y = req.velocity().y();
            ps->vel_z = req.velocity().z();
        }
        ps->move_state = req.state();

        // Build S_MOVE -- copy position values UNCHANGED
        game::S_Move s_move;
        s_move.set_player_id(player_id);
        if (req.has_position()) {
            auto* pos = s_move.mutable_position();
            pos->set_x(req.position().x());
            pos->set_y(req.position().y());  // timestamp low bits - PRESERVED
            pos->set_z(req.position().z());  // timestamp high bits - PRESERVED
        }
        s_move.set_rotation_y(req.rotation_y());
        if (req.has_velocity()) {
            auto* vel = s_move.mutable_velocity();
            vel->set_x(req.velocity().x());
            vel->set_y(req.velocity().y());
            vel->set_z(req.velocity().z());
        }
        s_move.set_state(req.state());

        auto buf = BuildPacket(pool, game::S_MOVE, s_move);
        if (buf) zone.BroadcastToAll(buf, player_id);
        break;
    }
    case game::C_ATTACK: {
        game::C_Attack req;
        if (!req.ParseFromArray(body, body_len)) return;

        const game::Vec3* fire_pos = req.has_fire_pos() ? &req.fire_pos() : nullptr;
        const game::Vec3* fire_dir = req.has_fire_dir() ? &req.fire_dir() : nullptr;
        ProcessAttack(zone, pool, player_id, req.target_id(), req.skill_id(),
                      fire_pos, fire_dir);
        break;
    }
    case game::C_CREATE_ROOM:
    case game::C_JOIN_ROOM:
        // Room creation/joining requires ZoneManager access.
        // For the separated architecture, these would need additional
        // coordination. For now, they are not supported in zone worker.
        // The benchmark client does not use these packets.
        break;
    default:
        break;
    }
}

} // namespace bench
