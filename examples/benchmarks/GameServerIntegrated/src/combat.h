#pragma once

#include "types.h"
#include "player.h"
#include "zone.h"
#include "packet_builder.h"

#include "Game.pb.h"
#include "Enum.pb.h"

#include <cstdint>

namespace bench {

inline void ProcessAttack(
    Zone& zone,
    servercore::buffer::BufferPool& pool,
    PlayerId attacker_id,
    uint64_t target_id,
    int32_t skill_id,
    const game::Vec3* fire_pos,
    const game::Vec3* fire_dir)
{
    // Validate both players exist under lock
    if (!zone.FindPlayer(attacker_id)) return;
    if (!zone.FindPlayer(target_id)) return;

    // Build S_ATTACK
    game::S_Attack s_attack;
    s_attack.set_attacker_id(attacker_id);
    s_attack.set_target_id(target_id);
    s_attack.set_skill_id(skill_id);
    if (fire_pos) *s_attack.mutable_fire_pos() = *fire_pos;
    if (fire_dir) *s_attack.mutable_fire_dir() = *fire_dir;

    auto attack_buf = BuildPacket(pool, game::S_ATTACK, s_attack);
    if (attack_buf) {
        zone.BroadcastToAll(attack_buf);
    }

    // Apply damage under the zone lock, collect info for damage packet
    int32_t remaining_hp = 0;
    bool is_dead = false;
    bool applied = zone.WithPlayer(target_id, [&](PlayerState& target) {
        int32_t damage = 10;
        target.hp -= damage;
        if (target.hp <= 0) {
            target.hp = 0;
            target.is_dead = true;
            is_dead = true;
            target.death_time = std::chrono::steady_clock::now();
        }
        remaining_hp = target.hp;
    });

    if (!applied) return;

    // Build S_DAMAGE
    game::S_Damage s_damage;
    s_damage.set_attacker_id(attacker_id);
    s_damage.set_target_id(target_id);
    s_damage.set_damage(10);
    s_damage.set_remaining_hp(remaining_hp);
    s_damage.set_skill_id(skill_id);
    s_damage.set_is_dead(is_dead);

    auto damage_buf = BuildPacket(pool, game::S_DAMAGE, s_damage);
    if (damage_buf) {
        zone.BroadcastToAll(damage_buf);
    }
}

} // namespace bench
