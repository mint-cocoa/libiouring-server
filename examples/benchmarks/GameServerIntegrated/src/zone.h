#pragma once

#include "types.h"
#include "player.h"

#include <servercore/buffer/SendBuffer.h>
#include <servercore/ring/IoRing.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace bench {

struct SendTarget;

class Zone {
public:
    explicit Zone(ZoneId id) : zone_id_(id) {}

    ZoneId Id() const { return zone_id_; }

    std::size_t PlayerCount() const {
        std::lock_guard lock(mutex_);
        return players_.size();
    }

    // Add player and broadcast S_SPAWN to existing players
    void AddPlayer(PlayerId pid, PlayerState state, servercore::buffer::BufferPool& pool);
    void RemovePlayer(PlayerId pid, servercore::buffer::BufferPool& pool);
    PlayerState* FindPlayer(PlayerId pid);

    // Execute a callback under the zone lock with a found player.
    // Returns false if the player was not found.
    template<typename Fn>
    bool WithPlayer(PlayerId pid, Fn&& fn) {
        std::lock_guard lock(mutex_);
        auto it = players_.find(pid);
        if (it == players_.end()) return false;
        fn(it->second);
        return true;
    }

    void BroadcastToAll(servercore::buffer::SendBufferRef buf, PlayerId exclude = 0);
    void SendToPlayer(PlayerId pid, servercore::buffer::SendBufferRef buf);

    // Thread-safe snapshot of players for read-only iteration
    std::unordered_map<PlayerId, PlayerState> PlayersSnapshot() const {
        std::lock_guard lock(mutex_);
        return players_;
    }

    void Tick(servercore::buffer::BufferPool& pool);

private:
    // Snapshot session weak_ptrs under lock for sending outside the lock.
    std::vector<SendTarget> SnapshotTargets(PlayerId exclude = 0) const;
    void DoSend(const PlayerState& ps, servercore::buffer::SendBufferRef buf);

    ZoneId zone_id_;
    mutable std::mutex mutex_;
    std::unordered_map<PlayerId, PlayerState> players_;
};

} // namespace bench
