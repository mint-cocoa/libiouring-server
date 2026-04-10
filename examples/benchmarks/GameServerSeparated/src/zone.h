#pragma once

#include "types.h"
#include "player.h"

#include <servercore/MpscQueue.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/ring/IoRing.h>

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace bench {

// Envelope for packets traveling from IO thread to Zone worker thread.
struct ZonePacket {
    PlayerId player_id = 0;
    uint16_t msg_id = 0;
    std::vector<std::byte> data;  // protobuf payload copy
};

class Zone {
public:
    explicit Zone(ZoneId id) : zone_id_(id) {}

    ZoneId Id() const { return zone_id_; }
    std::size_t PlayerCount() const { return players_.size(); }

    // --- Called from IO thread ---
    void PostPacket(ZonePacket pkt);

    // --- Called from Zone worker thread ---
    std::size_t DrainPacketQueue();
    void Tick(servercore::buffer::BufferPool& pool);

    // Player management (called from zone worker)
    void AddPlayer(PlayerId pid, PlayerState state, servercore::buffer::BufferPool& pool);
    void RemovePlayer(PlayerId pid, servercore::buffer::BufferPool& pool);
    PlayerState* FindPlayer(PlayerId pid);

    const std::unordered_map<PlayerId, PlayerState>& Players() const { return players_; }

    // Send helpers -- ALWAYS use RunOnRing (zone worker != IO thread)
    void BroadcastToAll(servercore::buffer::SendBufferRef buf, PlayerId exclude = 0);
    void SendToPlayer(PlayerId pid, servercore::buffer::SendBufferRef buf);

    void SetBufferPool(servercore::buffer::BufferPool* pool) { pool_ = pool; }
    servercore::buffer::BufferPool* GetBufferPool() const { return pool_; }

private:
    void DoSend(const PlayerState& ps, servercore::buffer::SendBufferRef buf);
    void HandlePacket(PlayerId player_id, uint16_t msg_id,
                      const std::byte* body, uint32_t body_len);

    ZoneId zone_id_;
    std::unordered_map<PlayerId, PlayerState> players_;
    servercore::MpscQueue<ZonePacket> packet_queue_;
    servercore::buffer::BufferPool* pool_ = nullptr;
};

} // namespace bench
