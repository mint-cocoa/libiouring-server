// BenchClient.cpp — Game protocol bot implementation.

#include "BenchClient.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

namespace bench {

using Clock = std::chrono::steady_clock;

// ── Helpers ───────────────────────────────────────────────────────

uint64_t BenchClient::NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count());
}

// ── Constructor ───────────────────────────────────────────────────

BenchClient::BenchClient(uint32_t conn_idx, int thread_id, const Config& cfg,
                         int global_conn_base,
                         std::atomic<uint32_t>* room_ids,
                         LiveStats* live)
    : conn_idx_(conn_idx)
    , thread_id_(thread_id)
    , cfg_(cfg)
    , live_(live)
    , room_ids_(room_ids)
{
    // Unique username: bench_T{thread}_C{conn}
    username_ = "bench_T" + std::to_string(thread_id) + "_C" + std::to_string(conn_idx);

    recv_buf_.resize(kRecvBufSize);
    broadcast_latency_ns_.reserve(4096);

    // Room assignment
    if (cfg_.rooms > 0 && room_ids_) {
        int total_conns = cfg_.threads * cfg_.conns;
        int bots_per_room = (total_conns + cfg_.rooms - 1) / cfg_.rooms;  // ceil
        int global_idx = global_conn_base + static_cast<int>(conn_idx);
        room_group_ = global_idx / bots_per_room;
        if (room_group_ >= cfg_.rooms) room_group_ = cfg_.rooms - 1;
        is_room_leader_ = (global_idx % bots_per_room == 0);
    }
}

// ── Connection ────────────────────────────────────────────────────

bool BenchClient::StartConnect(struct io_uring* ring, const struct sockaddr_in& addr) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        state_ = BotState::Done;
        return false;
    }

    int opt = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    auto* sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        close(fd_);
        fd_ = -1;
        state_ = BotState::Done;
        return false;
    }

    io_uring_prep_connect(sqe, fd_,
                          reinterpret_cast<const struct sockaddr*>(&addr),
                          sizeof(addr));
    io_uring_sqe_set_data64(sqe, MakeTag(conn_idx_, OpType::Connect));
    state_ = BotState::Connecting;
    return true;
}

void BenchClient::OnConnect(struct io_uring* ring, int res) {
    if (res < 0) {
        std::fprintf(stderr, "[%s] connect failed: %s\n",
                     username_.c_str(), strerror(-res));
        state_ = BotState::Done;
        if (live_) live_->failed.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    ever_connected_ = true;
    state_ = BotState::LoggingIn;
    if (live_) live_->connected.fetch_add(1, std::memory_order_relaxed);
    SubmitRecv(ring);
    SendLogin(ring);
}

// ── Recv ──────────────────────────────────────────────────────────

void BenchClient::SubmitRecv(struct io_uring* ring) {
    if (state_ == BotState::Done) return;

    auto* sqe = io_uring_get_sqe(ring);
    if (!sqe) return;

    io_uring_prep_recv(sqe, fd_,
                       recv_buf_.data() + recv_len_,
                       recv_buf_.size() - recv_len_, 0);
    io_uring_sqe_set_data64(sqe, MakeTag(conn_idx_, OpType::Recv));
}

void BenchClient::OnRecvComplete(struct io_uring* ring, int bytes) {
    if (bytes <= 0) {
        if (bytes < 0 && -bytes != ECONNRESET)
            std::fprintf(stderr, "[%s] recv error: %s\n",
                         username_.c_str(), strerror(-bytes));
        state_ = BotState::Done;
        return;
    }

    recv_len_ += static_cast<size_t>(bytes);

    // Parse complete packets
    while (recv_len_ >= kHeaderSize) {
        PacketHeader hdr;
        std::memcpy(&hdr, recv_buf_.data(), kHeaderSize);

        if (hdr.size < kHeaderSize || hdr.size > kRecvBufSize) {
            std::fprintf(stderr, "[%s] bad packet size=%u\n",
                         username_.c_str(), hdr.size);
            state_ = BotState::Done;
            return;
        }

        if (recv_len_ < hdr.size) break;  // incomplete

        size_t body_len = hdr.size - kHeaderSize;
        HandlePacket(ring, hdr.id, recv_buf_.data() + kHeaderSize, body_len);

        // Compact buffer
        recv_len_ -= hdr.size;
        if (recv_len_ > 0) {
            std::memmove(recv_buf_.data(),
                         recv_buf_.data() + hdr.size, recv_len_);
        }

        if (state_ == BotState::Done) return;
    }

    // Re-submit recv
    SubmitRecv(ring);
}

// ── Send ──────────────────────────────────────────────────────────

void BenchClient::QueueSend(struct io_uring* ring, std::vector<uint8_t> data) {
    if (state_ == BotState::Done) return;

    if (send_in_flight_) {
        // io_uring send references send_buf_ — append to separate pending buffer
        // to avoid vector reallocation invalidating the kernel's pointer.
        pending_buf_.insert(pending_buf_.end(), data.begin(), data.end());
    } else {
        send_buf_.insert(send_buf_.end(), data.begin(), data.end());
        SubmitSend(ring);
    }
}

void BenchClient::SubmitSend(struct io_uring* ring) {
    if (send_pos_ >= send_buf_.size()) {
        send_in_flight_ = false;
        return;
    }

    auto* sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        send_in_flight_ = false;
        return;
    }

    io_uring_prep_send(sqe, fd_,
                       send_buf_.data() + send_pos_,
                       send_buf_.size() - send_pos_, MSG_NOSIGNAL);
    io_uring_sqe_set_data64(sqe, MakeTag(conn_idx_, OpType::Send));
    send_in_flight_ = true;
}

void BenchClient::OnSendComplete(struct io_uring* ring, int bytes) {
    if (bytes < 0) {
        if (-bytes != ECONNRESET && -bytes != EPIPE)
            std::fprintf(stderr, "[%s] send error: %s\n",
                         username_.c_str(), strerror(-bytes));
        state_ = BotState::Done;
        return;
    }

    send_pos_ += static_cast<size_t>(bytes);

    if (send_pos_ >= send_buf_.size()) {
        // All data sent — swap in pending data if any
        send_buf_.clear();
        send_pos_ = 0;
        send_in_flight_ = false;

        if (!pending_buf_.empty()) {
            send_buf_.swap(pending_buf_);
            SubmitSend(ring);
        }
    } else {
        // Partial send, continue
        SubmitSend(ring);
    }
}

// ── Packet dispatch ───────────────────────────────────────────────

void BenchClient::HandlePacket(struct io_uring* ring, uint16_t pkt_id,
                                const uint8_t* body, size_t body_len) {
    switch (pkt_id) {
    case game::S_LOGIN:       HandleLogin(body, body_len); break;
    case game::S_ENTER_GAME:  HandleEnterGame(ring, body, body_len); break;
    case game::S_PLAYER_LIST: HandlePlayerList(body, body_len); break;
    case game::S_SPAWN:       HandleSpawn(body, body_len); break;
    case game::S_DESPAWN:     HandleDespawn(body, body_len); break;
    case game::S_MOVE:        HandleMove(body, body_len); break;
    case game::S_ATTACK:      HandleAttack(body, body_len); break;
    case game::S_DAMAGE:      HandleDamage(body, body_len); break;
    case game::S_CREATE_ROOM: HandleCreateRoom(body, body_len); break;
    case game::S_JOIN_ROOM:   HandleJoinRoom(body, body_len); break;
    case game::S_SKILL_DATA:  break;  // ignored
    case game::S_RESPAWN:     break;  // ignored
    default: break;
    }
}

// ── Auth handlers ─────────────────────────────────────────────────

void BenchClient::HandleLogin(const uint8_t* body, size_t len) {
    game::S_Login res;
    if (!ParsePacket(body, len, res)) return;

    uint64_t now = NowNs();
    if (login_send_ns_ > 0)
        login_rtt_ns_ = now - login_send_ns_;

    if (!res.success()) {
        std::fprintf(stderr, "[%s] login rejected\n", username_.c_str());
        state_ = BotState::Done;
        return;
    }

    my_player_id_ = res.player_id();
    state_ = BotState::EnteringGame;
}

void BenchClient::HandleEnterGame(struct io_uring* ring, const uint8_t* body, size_t len) {
    game::S_EnterGame res;
    if (!ParsePacket(body, len, res)) return;

    uint64_t now = NowNs();
    if (enter_game_send_ns_ > 0)
        enter_rtt_ns_ = now - enter_game_send_ns_;

    if (!res.success()) {
        std::fprintf(stderr, "[%s] enter game rejected\n", username_.c_str());
        state_ = BotState::Done;
        return;
    }

    ever_in_game_ = true;
    if (live_) live_->in_game.fetch_add(1, std::memory_order_relaxed);

    // Room mode: leader creates, follower waits to join
    if (cfg_.rooms > 0 && room_ids_ && room_group_ >= 0) {
        if (is_room_leader_) {
            SendCreateRoom(ring);
            state_ = BotState::CreatingRoom;
        } else {
            state_ = BotState::JoiningRoom;
        }
    } else {
        state_ = BotState::Playing;
        last_move_ns_ = NowNs();
        last_attack_ns_ = NowNs();
    }
}

void BenchClient::HandleCreateRoom(const uint8_t* body, size_t len) {
    game::S_CreateRoom res;
    if (!ParsePacket(body, len, res)) return;

    if (!res.success()) {
        std::fprintf(stderr, "[%s] create room failed\n", username_.c_str());
        state_ = BotState::Done;
        return;
    }

    uint32_t room_id = res.zone_id();

    // Publish room_id so followers can see it
    if (room_ids_ && room_group_ >= 0) {
        room_ids_[room_group_].store(room_id, std::memory_order_release);
    }

    // Clear lobby players, will get new list from room
    known_players_.clear();

    state_ = BotState::Playing;
    last_move_ns_ = NowNs();
    last_attack_ns_ = NowNs();
}

void BenchClient::HandleJoinRoom(const uint8_t* body, size_t len) {
    game::S_JoinRoom res;
    if (!ParsePacket(body, len, res)) return;

    if (!res.success()) {
        // Room might be full or not found; retry later or give up
        std::fprintf(stderr, "[%s] join room failed: %s\n",
                     username_.c_str(), res.error().c_str());
        state_ = BotState::Done;
        return;
    }

    // Clear lobby players, will get new list from room
    known_players_.clear();

    state_ = BotState::Playing;
    last_move_ns_ = NowNs();
    last_attack_ns_ = NowNs();
}

void BenchClient::HandlePlayerList(const uint8_t* body, size_t len) {
    game::S_PlayerList msg;
    if (!ParsePacket(body, len, msg)) return;

    if (measuring_) rx_player_list_++;

    for (const auto& p : msg.players()) {
        if (p.player_id() != my_player_id_) {
            auto it = std::find(known_players_.begin(), known_players_.end(), p.player_id());
            if (it == known_players_.end())
                known_players_.push_back(p.player_id());
        }
    }
}

void BenchClient::HandleSpawn(const uint8_t* body, size_t len) {
    game::S_Spawn msg;
    if (!ParsePacket(body, len, msg)) return;

    if (measuring_) rx_spawn_++;

    uint64_t pid = msg.player().player_id();
    if (pid != my_player_id_) {
        auto it = std::find(known_players_.begin(), known_players_.end(), pid);
        if (it == known_players_.end())
            known_players_.push_back(pid);
    }
}

void BenchClient::HandleDespawn(const uint8_t* body, size_t len) {
    game::S_Despawn msg;
    if (!ParsePacket(body, len, msg)) return;

    if (measuring_) rx_despawn_++;

    uint64_t pid = msg.player_id();
    auto it = std::find(known_players_.begin(), known_players_.end(), pid);
    if (it != known_players_.end())
        known_players_.erase(it);
}

void BenchClient::HandleMove(const uint8_t* body, size_t len) {
    game::S_Move msg;
    if (!ParsePacket(body, len, msg)) return;

    if (measuring_) rx_move_++;
    if (live_) live_->rx_move.fetch_add(1, std::memory_order_relaxed);

    // Decode broadcast timestamp from position.y (low 32) and position.z (high 32)
    if (msg.has_position()) {
        float fy = msg.position().y();
        float fz = msg.position().z();

        uint32_t lo = std::bit_cast<uint32_t>(fy);
        uint32_t hi = std::bit_cast<uint32_t>(fz);
        uint64_t send_ns = (static_cast<uint64_t>(hi) << 32) | lo;

        // Sanity check: if timestamp looks reasonable (within last 60 seconds)
        uint64_t now = NowNs();
        if (send_ns > 0 && send_ns < now && (now - send_ns) < 60'000'000'000ULL) {
            uint64_t lat = now - send_ns;
            if (measuring_)
                broadcast_latency_ns_.push_back(lat);
            if (live_) {
                live_->latency_sum_ns.fetch_add(lat, std::memory_order_relaxed);
                live_->latency_count.fetch_add(1, std::memory_order_relaxed);
                live_->UpdateLatencyMax(lat);
            }
        }
    }
}

void BenchClient::HandleAttack(const uint8_t* body, size_t len) {
    game::S_Attack msg;
    if (!ParsePacket(body, len, msg)) return;

    if (measuring_) rx_attack_++;
    if (live_) live_->rx_attack.fetch_add(1, std::memory_order_relaxed);
}

void BenchClient::HandleDamage(const uint8_t* body, size_t len) {
    game::S_Damage msg;
    if (!ParsePacket(body, len, msg)) return;

    if (measuring_) rx_damage_++;
    if (live_) live_->rx_damage.fetch_add(1, std::memory_order_relaxed);
}

// ── Send packets ──────────────────────────────────────────────────

void BenchClient::SendLogin(struct io_uring* ring) {
    game::C_Login msg;
    msg.set_username(username_);
    msg.set_password("bench");

    login_send_ns_ = NowNs();
    QueueSend(ring, MakePacket(game::C_LOGIN, msg));
}

void BenchClient::SendEnterGame(struct io_uring* ring) {
    game::C_EnterGame msg;

    enter_game_send_ns_ = NowNs();
    QueueSend(ring, MakePacket(game::C_ENTER_GAME, msg));
}

void BenchClient::SendCreateRoom(struct io_uring* ring) {
    game::C_CreateRoom msg;
    msg.set_room_name("bench_room_" + std::to_string(room_group_));
    QueueSend(ring, MakePacket(game::C_CREATE_ROOM, msg));
}

void BenchClient::SendJoinRoom(struct io_uring* ring, uint32_t room_id) {
    game::C_JoinRoom msg;
    msg.set_zone_id(room_id);
    QueueSend(ring, MakePacket(game::C_JOIN_ROOM, msg));
}

void BenchClient::SendMove(struct io_uring* ring, uint64_t now_ns) {
    game::C_Move msg;

    // Encode timestamp into position for broadcast latency measurement
    // position.y = low 32 bits, position.z = high 32 bits
    uint32_t lo = static_cast<uint32_t>(now_ns & 0xFFFFFFFF);
    uint32_t hi = static_cast<uint32_t>(now_ns >> 32);

    auto* pos = msg.mutable_position();
    // Use a small random x for some variation
    pos->set_x(static_cast<float>(conn_idx_) * 2.0f);
    pos->set_y(std::bit_cast<float>(lo));
    pos->set_z(std::bit_cast<float>(hi));
    msg.set_rotation_y(0.0f);
    msg.set_state(1);  // walking

    if (measuring_) tx_move_++;
    if (live_) live_->tx_move.fetch_add(1, std::memory_order_relaxed);
    QueueSend(ring, MakePacket(game::C_MOVE, msg));
}

void BenchClient::SendAttack(struct io_uring* ring) {
    if (known_players_.empty()) return;

    // Pick a random target
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, known_players_.size() - 1);
    uint64_t target = known_players_[dist(rng)];

    game::C_Attack msg;
    msg.set_target_id(target);
    msg.set_skill_id(1);

    if (measuring_) tx_attack_++;
    if (live_) live_->tx_attack.fetch_add(1, std::memory_order_relaxed);
    QueueSend(ring, MakePacket(game::C_ATTACK, msg));
}

// ── Gameplay timer ────────────────────────────────────────────────

void BenchClient::MaybeSendGameplay(struct io_uring* ring, uint64_t now_ns, bool measuring) {
    measuring_ = measuring;

    // EnteringGame: send C_ENTER_GAME once
    if (state_ == BotState::EnteringGame && !enter_game_sent_) {
        SendEnterGame(ring);
        enter_game_sent_ = true;
        return;
    }

    // CreatingRoom: already sent in HandleEnterGame, just waiting
    if (state_ == BotState::CreatingRoom) {
        return;
    }

    // JoiningRoom: poll for room_id, then send C_JOIN_ROOM once
    if (state_ == BotState::JoiningRoom && !room_request_sent_) {
        if (room_ids_ && room_group_ >= 0) {
            uint32_t rid = room_ids_[room_group_].load(std::memory_order_acquire);
            if (rid != 0) {
                SendJoinRoom(ring, rid);
                room_request_sent_ = true;
            }
        }
        return;
    }

    if (state_ != BotState::Playing) return;

    uint64_t move_interval_ns = static_cast<uint64_t>(cfg_.move_interval_ms) * 1'000'000ULL;
    uint64_t attack_interval_ns = static_cast<uint64_t>(cfg_.attack_interval_ms) * 1'000'000ULL;

    // C_MOVE
    if (now_ns - last_move_ns_ >= move_interval_ns) {
        SendMove(ring, now_ns);
        last_move_ns_ = now_ns;
    }

    // C_ATTACK
    if (!cfg_.no_attack && now_ns - last_attack_ns_ >= attack_interval_ns) {
        SendAttack(ring);
        last_attack_ns_ = now_ns;
    }
}

// ── Cleanup ───────────────────────────────────────────────────────

void BenchClient::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    state_ = BotState::Done;
}

// ── Stats flush ───────────────────────────────────────────────────

void BenchClient::FlushStats(ThreadStats& out) const {
    if (login_rtt_ns_ > 0)
        out.login_rtt_ns.push_back(login_rtt_ns_);
    if (enter_rtt_ns_ > 0)
        out.enter_rtt_ns.push_back(enter_rtt_ns_);

    out.broadcast_latency_ns.insert(out.broadcast_latency_ns.end(),
                                     broadcast_latency_ns_.begin(),
                                     broadcast_latency_ns_.end());

    out.tx_move   += tx_move_;
    out.tx_attack += tx_attack_;

    out.rx_move        += rx_move_;
    out.rx_attack      += rx_attack_;
    out.rx_damage      += rx_damage_;
    out.rx_spawn       += rx_spawn_;
    out.rx_despawn     += rx_despawn_;
    out.rx_player_list += rx_player_list_;

    if (ever_connected_)
        out.connected++;
    else
        out.failed++;

    if (ever_in_game_)
        out.in_game++;
}

} // namespace bench
