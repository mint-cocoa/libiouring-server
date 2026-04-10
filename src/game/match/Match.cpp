#include <servergame/Match.h>
#include <servergame/WsFrameBuilder.h>

#include <nlohmann/json.hpp>

using namespace servercore;

namespace servergame::match {

Match::Match(uint64_t id, std::unique_ptr<MatchHandler> handler,
             MatchConfig config, NetDispatcher dispatcher,
             PlayerRegistry& registry, job::GlobalQueue& gq,
             buffer::BufferPool& pool)
    : JobQueue(gq)
    , id_(id)
    , config_(std::move(config))
    , handler_(std::move(handler))
    , net_dispatcher_(std::move(dispatcher))
    , registry_(registry)
    , pool_(pool)
    , last_tick_(std::chrono::steady_clock::now())
{
    handler_->Init(*this);
}

Match::~Match() = default;

void Match::RequestJoin(PlayerId pid, ContextId reactor) {
    Push([this, pid, reactor] { HandleJoin(pid, reactor); });
}

void Match::RequestLeave(PlayerId pid) {
    Push([this, pid] { HandleLeave(pid); });
}

void Match::DispatchMessage(PlayerId sender, int64_t opcode,
                            std::vector<std::byte> data) {
    Push([this, sender, opcode, d = std::move(data)]() mutable {
        HandleMessage(sender, opcode, std::move(d));
    });
}

void Match::Terminate() {
    Push([this] { HandleTerminate(); });
}

void Match::ScheduleTick(job::JobTimer& timer) {
    if (config_.tick_rate == 0) return;
    auto interval = std::chrono::milliseconds(1000 / config_.tick_rate);
    timer.Reserve(interval, weak_from_this(), [this, &timer] { HandleTick(timer); });
}

void Match::HandleJoin(PlayerId pid, ContextId reactor) {
    if (!active_) return;
    if (config_.max_players > 0 && players_.size() >= config_.max_players) return;
    if (!handler_->JoinAttempt(*this, pid)) return;

    players_[pid] = reactor;
    registry_.UpdateStatus(pid, "in_game");
    registry_.UpdateMatch(pid, id_);
    handler_->OnJoin(*this, pid);
}

void Match::HandleLeave(PlayerId pid) {
    if (players_.erase(pid) > 0) {
        registry_.UpdateStatus(pid, "online");
        registry_.UpdateMatch(pid, 0);
        handler_->OnLeave(*this, pid);
    }
}

void Match::HandleMessage(PlayerId sender, int64_t opcode,
                          std::vector<std::byte> data) {
    handler_->OnMessage(*this, sender, opcode,
                        std::span<const std::byte>(data));
}

void Match::HandleTick(job::JobTimer& timer) {
    if (!active_) return;
    auto now = std::chrono::steady_clock::now();
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_);
    last_tick_ = now;
    handler_->OnTick(*this, dt);
    ScheduleTick(timer);
}

void Match::HandleTerminate() {
    handler_->OnTerminate(*this);
    active_ = false;
    for (auto& [pid, reactor] : players_) {
        registry_.UpdateStatus(pid, "online");
        registry_.UpdateMatch(pid, 0);
    }
    players_.clear();
}

void Match::SendToPlayer(PlayerId pid, int64_t opcode,
                         std::span<const std::byte> data) {
    auto it = players_.find(pid);
    if (it == players_.end()) return;
    SendToReactor(it->second, pid, opcode, data);
}

void Match::BroadcastAll(int64_t opcode, std::span<const std::byte> data) {
    for (auto& [pid, reactor] : players_)
        SendToReactor(reactor, pid, opcode, data);
}

void Match::BroadcastExcept(PlayerId exclude, int64_t opcode,
                            std::span<const std::byte> data) {
    for (auto& [pid, reactor] : players_) {
        if (pid != exclude)
            SendToReactor(reactor, pid, opcode, data);
    }
}

void Match::SendToReactor(ContextId reactor, PlayerId pid, int64_t opcode,
                          std::span<const std::byte> data) {
    nlohmann::json j;
    j["type"] = "match.data";
    j["match_id"] = id_;
    j["sender"] = pid;
    j["opcode"] = opcode;
    j["data_size"] = data.size();

    auto buf = BuildWsTextFrame(pool_, j.dump());
    if (buf) {
        net_dispatcher_(reactor,
                       net::SendToPlayerCmd{pid, std::move(buf)});
    }
}

} // namespace servergame::match
