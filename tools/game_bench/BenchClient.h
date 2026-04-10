#pragma once

// BenchClient.h — Game protocol bot with io_uring-driven state machine.

#include "Config.h"
#include "LiveStats.h"
#include "Proto.h"
#include "Stats.h"

#include "Auth.pb.h"
#include "Common.pb.h"
#include "Game.pb.h"

#include <liburing.h>
#include <netinet/in.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace bench {

// ── io_uring tag encoding ─────────────────────────────────────────

enum class OpType : uint8_t {
    Connect = 0,
    Send    = 1,
    Recv    = 2,
};

inline uint64_t MakeTag(uint32_t conn_idx, OpType op) {
    return (static_cast<uint64_t>(conn_idx) << 16) | (static_cast<uint64_t>(op) << 8);
}

inline uint32_t TagConnIdx(uint64_t tag) { return static_cast<uint32_t>(tag >> 16); }
inline OpType   TagOp(uint64_t tag)      { return static_cast<OpType>((tag >> 8) & 0xFF); }

// ── Bot state ─────────────────────────────────────────────────────

enum class BotState {
    Connecting,
    LoggingIn,
    EnteringGame,
    CreatingRoom,   // leader: sent C_CREATE_ROOM, waiting S_CREATE_ROOM
    JoiningRoom,    // follower: waiting for room_id, then C_JOIN_ROOM
    Playing,
    Done,
};

// ── BenchClient ───────────────────────────────────────────────────

class BenchClient {
public:
    BenchClient(uint32_t conn_idx, int thread_id, const Config& cfg,
                int global_conn_base,
                std::atomic<uint32_t>* room_ids,
                LiveStats* live = nullptr);

    // Non-copyable, movable
    BenchClient(const BenchClient&) = delete;
    BenchClient& operator=(const BenchClient&) = delete;
    BenchClient(BenchClient&&) = default;
    BenchClient& operator=(BenchClient&&) = default;

    // Create socket + submit io_uring connect SQE
    bool StartConnect(struct io_uring* ring, const struct sockaddr_in& addr);

    // CQE handlers
    void OnConnect(struct io_uring* ring, int res);
    void OnRecvComplete(struct io_uring* ring, int bytes);
    void OnSendComplete(struct io_uring* ring, int bytes);

    // Timer-based gameplay actions (called from main loop)
    void MaybeSendGameplay(struct io_uring* ring, uint64_t now_ns, bool measuring);

    // Cleanup
    void Close();

    // Accessors
    BotState state() const { return state_; }
    int fd() const { return fd_; }
    uint32_t conn_idx() const { return conn_idx_; }

    // Collect stats into thread-level accumulator
    void FlushStats(ThreadStats& out) const;

private:
    // Packet handling dispatch
    void HandlePacket(struct io_uring* ring, uint16_t pkt_id,
                      const uint8_t* body, size_t body_len);

    // Individual handlers
    void HandleLogin(const uint8_t* body, size_t len);
    void HandleEnterGame(struct io_uring* ring, const uint8_t* body, size_t len);
    void HandlePlayerList(const uint8_t* body, size_t len);
    void HandleSpawn(const uint8_t* body, size_t len);
    void HandleDespawn(const uint8_t* body, size_t len);
    void HandleMove(const uint8_t* body, size_t len);
    void HandleAttack(const uint8_t* body, size_t len);
    void HandleDamage(const uint8_t* body, size_t len);
    void HandleCreateRoom(const uint8_t* body, size_t len);
    void HandleJoinRoom(const uint8_t* body, size_t len);

    // io_uring submit helpers
    void SubmitRecv(struct io_uring* ring);
    void SubmitSend(struct io_uring* ring);
    void QueueSend(struct io_uring* ring, std::vector<uint8_t> data);

    // Send specific packets
    void SendLogin(struct io_uring* ring);
    void SendEnterGame(struct io_uring* ring);
    void SendMove(struct io_uring* ring, uint64_t now_ns);
    void SendAttack(struct io_uring* ring);
    void SendCreateRoom(struct io_uring* ring);
    void SendJoinRoom(struct io_uring* ring, uint32_t room_id);

    static uint64_t NowNs();

    // Identity
    uint32_t conn_idx_;
    int      thread_id_;
    std::string username_;
    const Config& cfg_;
    LiveStats* live_;

    // Socket
    int fd_ = -1;
    BotState state_ = BotState::Connecting;

    // Player identity from server
    uint64_t my_player_id_ = 0;

    // Known players (for attack targeting)
    std::vector<uint64_t> known_players_;

    // Recv buffer (128KB to handle large S_PLAYER_LIST in lobby)
    static constexpr size_t kRecvBufSize = 131072;
    std::vector<uint8_t> recv_buf_;
    size_t recv_len_ = 0;

    // Send: double-buffer to avoid reallocation while io_uring send is in-flight.
    // send_buf_ is owned by the kernel until OnSendComplete; new data goes to pending_buf_.
    std::vector<uint8_t> send_buf_;
    std::vector<uint8_t> pending_buf_;
    size_t send_pos_ = 0;
    bool   send_in_flight_ = false;

    // Timers (ns)
    uint64_t last_move_ns_   = 0;
    uint64_t last_attack_ns_ = 0;

    // Handshake timestamps (for RTT)
    uint64_t login_send_ns_      = 0;
    uint64_t enter_game_send_ns_ = 0;

    // One-shot send flags
    bool enter_game_sent_  = false;
    bool room_request_sent_ = false;

    // Room assignment (--rooms mode)
    int  room_group_      = -1;  // which room this bot belongs to
    bool is_room_leader_  = false;  // first bot in group creates room
    std::atomic<uint32_t>* room_ids_ = nullptr;  // shared array

    // Stats (local to this client, flushed to ThreadStats)
    uint64_t login_rtt_ns_ = 0;
    uint64_t enter_rtt_ns_ = 0;

    std::vector<uint64_t> broadcast_latency_ns_;

    uint64_t tx_move_   = 0;
    uint64_t tx_attack_ = 0;

    uint64_t rx_move_        = 0;
    uint64_t rx_attack_      = 0;
    uint64_t rx_damage_      = 0;
    uint64_t rx_spawn_       = 0;
    uint64_t rx_despawn_     = 0;
    uint64_t rx_player_list_ = 0;

    bool measuring_ = false;
    bool ever_connected_ = false;  // true if connect succeeded
    bool ever_in_game_   = false;  // true if entered Playing state
};

} // namespace bench
