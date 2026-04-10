#include "bench_session.h"
#include "packet_handler.h"
#include "packet_framing.h"
#include "zone_manager.h"

namespace bench {

BenchSession::BenchSession(int fd, servercore::ring::IoRing& ring,
                           servercore::buffer::BufferPool& pool,
                           ZoneManager& zone_mgr, WorkerId worker_id)
    : Session(fd, ring, pool)
    , zone_mgr_(zone_mgr)
    , worker_id_(worker_id)
{
}

void BenchSession::OnRecv(std::span<const std::byte> data) {
    bool ok = DrainPackets(
        recv_buf_, data,
        [this](uint16_t msg_id, const std::byte* body, uint32_t body_len) {
            OnPacket(msg_id, body, body_len);
        });

    if (!ok) {
        Disconnect();
    }
}

void BenchSession::OnPacket(uint16_t msg_id, const std::byte* body, uint32_t body_len) {
    // Get shared_ptr via EventHandler's enable_shared_from_this
    auto session_ptr = std::static_pointer_cast<servercore::io::Session>(
        shared_from_this());

    HandlePacket(msg_id, body, body_len,
                 session_ptr, Pool(), zone_mgr_,
                 player_id_, zone_id_, worker_id_,
                 logged_in_, in_game_);
}

void BenchSession::OnDisconnected() {
    if (in_game_) {
        auto* zone = zone_mgr_.FindZone(zone_id_);
        if (zone) {
            zone->RemovePlayer(player_id_, Pool());
        }
        in_game_ = false;
    }
}

} // namespace bench
