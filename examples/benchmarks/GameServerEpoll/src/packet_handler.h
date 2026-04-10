#pragma once

#include "types.h"
#include "player_epoll.h"
#include "zone.h"
#include "zone_manager.h"
#include "combat.h"
#include "packet_builder.h"
#include "epoll_connection.h"

#include "Enum.pb.h"
#include "Auth.pb.h"
#include "Game.pb.h"
#include "Common.pb.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace bench {

inline std::atomic<uint64_t> g_next_player_id{1};

inline game::PlayerInfo MakePlayerInfo(const EpollPlayerState& ps) {
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

inline void HandlePacket(
    uint16_t msg_id,
    const std::byte* body,
    uint32_t body_len,
    EpollConnection& conn,
    ZoneManager& zone_mgr,
    PlayerId& player_id,
    ZoneId& zone_id,
    WorkerId worker_id,
    bool& logged_in,
    bool& in_game)
{
    switch (msg_id) {
    case game::C_LOGIN: {
        game::C_Login req;
        if (!req.ParseFromArray(body, body_len)) return;

        player_id = g_next_player_id.fetch_add(1, std::memory_order_relaxed);
        logged_in = true;

        game::S_Login res;
        res.set_success(true);
        res.set_player_id(player_id);

        conn.QueueSend(BuildPacket(game::S_LOGIN, res));
        break;
    }
    case game::C_ENTER_GAME: {
        if (!logged_in) return;

        zone_id = 0;
        auto* zone = zone_mgr.GetDefaultZone();
        if (!zone) return;

        EpollPlayerState ps;
        ps.player_id = player_id;
        ps.name = "Player_" + std::to_string(player_id);
        ps.hp = 100;
        ps.max_hp = 100;
        ps.zone_id = zone_id;
        ps.connection = &conn;
        ps.worker_id = worker_id;

        // Reply S_ENTER_GAME
        game::S_EnterGame res;
        res.set_success(true);
        *res.mutable_player() = MakePlayerInfo(ps);
        auto* map_data = res.mutable_map_data();
        map_data->set_grid_width(10);
        map_data->set_grid_height(10);
        map_data->set_cell_size(1.0f);

        conn.QueueSend(BuildPacket(game::S_ENTER_GAME, res));

        {
            // Hold zone mutex while iterating Players() and adding
            std::lock_guard lock(zone->Mutex());

            // Send S_PLAYER_LIST with existing players
            game::S_PlayerList player_list;
            for (auto& [pid, existing] : zone->Players()) {
                *player_list.add_players() = MakePlayerInfo(existing);
            }
            conn.QueueSend(BuildPacket(game::S_PLAYER_LIST, player_list));
        }

        // Add to zone (broadcasts S_SPAWN to existing players)
        zone->AddPlayer(player_id, ps);

        in_game = true;
        break;
    }
    case game::C_MOVE: {
        if (!in_game) return;

        game::C_Move req;
        if (!req.ParseFromArray(body, body_len)) return;

        auto* zone = zone_mgr.FindZone(zone_id);
        if (!zone) return;

        // Hold zone mutex while accessing player state and broadcasting
        std::lock_guard lock(zone->Mutex());

        auto* ps = zone->FindPlayer(player_id);
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
            pos->set_y(req.position().y());   // timestamp low bits - PRESERVED
            pos->set_z(req.position().z());   // timestamp high bits - PRESERVED
        }
        s_move.set_rotation_y(req.rotation_y());
        if (req.has_velocity()) {
            auto* vel = s_move.mutable_velocity();
            vel->set_x(req.velocity().x());
            vel->set_y(req.velocity().y());
            vel->set_z(req.velocity().z());
        }
        s_move.set_state(req.state());

        auto pkt = BuildPacket(game::S_MOVE, s_move);
        zone->BroadcastToAll(pkt, player_id);
        break;
    }
    case game::C_ATTACK: {
        if (!in_game) return;

        game::C_Attack req;
        if (!req.ParseFromArray(body, body_len)) return;

        auto* zone = zone_mgr.FindZone(zone_id);
        if (!zone) return;

        const game::Vec3* fire_pos = req.has_fire_pos() ? &req.fire_pos() : nullptr;
        const game::Vec3* fire_dir = req.has_fire_dir() ? &req.fire_dir() : nullptr;
        ProcessAttack(*zone, player_id, req.target_id(), req.skill_id(),
                      fire_pos, fire_dir);
        break;
    }
    case game::C_CREATE_ROOM: {
        if (!in_game) return;

        game::C_CreateRoom req;
        if (!req.ParseFromArray(body, body_len)) return;

        // Remove from current zone
        auto* old_zone = zone_mgr.FindZone(zone_id);
        if (old_zone) {
            old_zone->RemovePlayer(player_id);
        }

        // Create new zone
        ZoneId new_zone_id = zone_mgr.CreateZone();
        zone_id = new_zone_id;

        auto* new_zone = zone_mgr.FindZone(new_zone_id);
        if (!new_zone) return;

        EpollPlayerState ps;
        ps.player_id = player_id;
        ps.name = "Player_" + std::to_string(player_id);
        ps.hp = 100;
        ps.max_hp = 100;
        ps.zone_id = new_zone_id;
        ps.connection = &conn;
        ps.worker_id = worker_id;

        new_zone->AddPlayer(player_id, ps);

        game::S_CreateRoom res;
        res.set_success(true);
        res.set_zone_id(new_zone_id);
        *res.mutable_player() = MakePlayerInfo(ps);
        auto* map_data = res.mutable_map_data();
        map_data->set_grid_width(10);
        map_data->set_grid_height(10);
        map_data->set_cell_size(1.0f);

        conn.QueueSend(BuildPacket(game::S_CREATE_ROOM, res));
        break;
    }
    case game::C_JOIN_ROOM: {
        if (!in_game) return;

        game::C_JoinRoom req;
        if (!req.ParseFromArray(body, body_len)) return;

        auto* target_zone = zone_mgr.FindZone(req.zone_id());
        if (!target_zone) {
            game::S_JoinRoom res;
            res.set_success(false);
            res.set_error("Zone not found");
            conn.QueueSend(BuildPacket(game::S_JOIN_ROOM, res));
            return;
        }

        // Remove from current zone
        auto* old_zone = zone_mgr.FindZone(zone_id);
        if (old_zone) {
            old_zone->RemovePlayer(player_id);
        }

        zone_id = req.zone_id();

        EpollPlayerState ps;
        ps.player_id = player_id;
        ps.name = "Player_" + std::to_string(player_id);
        ps.hp = 100;
        ps.max_hp = 100;
        ps.zone_id = zone_id;
        ps.connection = &conn;
        ps.worker_id = worker_id;

        {
            // Hold zone mutex while iterating Players()
            std::lock_guard lock(target_zone->Mutex());

            // Send S_PLAYER_LIST before adding (existing players)
            game::S_PlayerList player_list;
            for (auto& [pid, existing] : target_zone->Players()) {
                *player_list.add_players() = MakePlayerInfo(existing);
            }
            conn.QueueSend(BuildPacket(game::S_PLAYER_LIST, player_list));
        }

        target_zone->AddPlayer(player_id, ps);

        game::S_JoinRoom res;
        res.set_success(true);
        res.set_zone_id(zone_id);
        *res.mutable_player() = MakePlayerInfo(ps);
        auto* map_data = res.mutable_map_data();
        map_data->set_grid_width(10);
        map_data->set_grid_height(10);
        map_data->set_cell_size(1.0f);

        conn.QueueSend(BuildPacket(game::S_JOIN_ROOM, res));
        break;
    }
    default:
        break;
    }
}

} // namespace bench
