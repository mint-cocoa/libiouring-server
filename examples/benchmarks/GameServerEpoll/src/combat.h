#pragma once

#include "types.h"
#include "player_epoll.h"
#include "zone.h"
#include "packet_builder.h"

#include "Game.pb.h"
#include "Enum.pb.h"

#include <cstdint>
#include <mutex>

namespace bench {

inline void ProcessAttack(
    Zone& zone,
    PlayerId attacker_id,
    uint64_t target_id,
    int32_t skill_id,
    const game::Vec3* fire_pos,
    const game::Vec3* fire_dir)
{
    std::lock_guard<std::recursive_mutex> lock(zone.Mutex());

    auto* attacker = zone.FindPlayer(attacker_id);
    if (!attacker) return;

    auto* target = zone.FindPlayer(target_id);
    if (!target) return;

    // Build S_ATTACK
    game::S_Attack s_attack;
    s_attack.set_attacker_id(attacker_id);
    s_attack.set_target_id(target_id);
    s_attack.set_skill_id(skill_id);
    if (fire_pos) *s_attack.mutable_fire_pos() = *fire_pos;
    if (fire_dir) *s_attack.mutable_fire_dir() = *fire_dir;

    auto attack_pkt = BuildPacket(game::S_ATTACK, s_attack);
    zone.BroadcastToAll(attack_pkt);

    // Calculate damage
    int32_t damage = 10;
    target->hp -= damage;
    bool is_dead = false;
    if (target->hp <= 0) {
        target->hp = 0;
        target->is_dead = true;
        is_dead = true;
        target->death_time = std::chrono::steady_clock::now();
    }

    // Build S_DAMAGE
    game::S_Damage s_damage;
    s_damage.set_attacker_id(attacker_id);
    s_damage.set_target_id(target_id);
    s_damage.set_damage(damage);
    s_damage.set_remaining_hp(target->hp);
    s_damage.set_skill_id(skill_id);
    s_damage.set_is_dead(is_dead);

    auto damage_pkt = BuildPacket(game::S_DAMAGE, s_damage);
    zone.BroadcastToAll(damage_pkt);
}

} // namespace bench
