#pragma once

#include <servercore/io/Session.h>
#include <servercore/buffer/RecvBuffer.h>
#include <servercore/ring/IoRing.h>
#include "types.h"

#include <cstdint>
#include <memory>
#include <span>

namespace bench {

class Zone;
class ZoneManager;

class BenchSession : public servercore::io::Session {
public:
    BenchSession(int fd, servercore::ring::IoRing& ring,
                 servercore::buffer::BufferPool& pool,
                 ZoneManager& zone_mgr, WorkerId worker_id);

    PlayerId GetPlayerId() const { return player_id_; }
    ZoneId GetZoneId() const { return zone_id_; }
    void SetZoneId(ZoneId zid) { zone_id_ = zid; }

protected:
    void OnRecv(std::span<const std::byte> data) override;
    void OnDisconnected() override;

private:
    void OnPacket(uint16_t msg_id, const std::byte* body, uint32_t body_len);

    servercore::buffer::RecvBuffer recv_buf_;
    ZoneManager& zone_mgr_;
    PlayerId player_id_ = 0;
    ZoneId zone_id_ = 0;
    WorkerId worker_id_ = 0;
    bool logged_in_ = false;
    bool in_game_ = false;
};

} // namespace bench
