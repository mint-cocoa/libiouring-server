#pragma once

#include "types.h"
#include "player_epoll.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace bench {

class Zone {
public:
    explicit Zone(ZoneId id) : zone_id_(id) {}

    ZoneId Id() const { return zone_id_; }
    std::size_t PlayerCount() const { return players_.size(); }

    void AddPlayer(PlayerId pid, EpollPlayerState state);
    void RemovePlayer(PlayerId pid);
    EpollPlayerState* FindPlayer(PlayerId pid);

    void BroadcastToAll(const std::vector<uint8_t>& pkt, PlayerId exclude = 0);
    void SendToPlayer(PlayerId pid, const std::vector<uint8_t>& pkt);

    const std::unordered_map<PlayerId, EpollPlayerState>& Players() const { return players_; }

    std::recursive_mutex& Mutex() { return mutex_; }

    void Tick();

private:
    void BroadcastToAllLocked(const std::vector<uint8_t>& pkt, PlayerId exclude = 0);

    mutable std::recursive_mutex mutex_;
    ZoneId zone_id_;
    std::unordered_map<PlayerId, EpollPlayerState> players_;
};

} // namespace bench
