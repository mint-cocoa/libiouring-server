#pragma once

#include <servergame/NetDispatchFn.h>
#include <servergame/PlayerRegistry.h>
#include <servergame/MatchConfig.h>
#include <servergame/MatchHandler.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/job/JobQueue.h>
#include <servercore/job/JobTimer.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace servergame::match {

using servercore::PlayerId;
using servercore::ContextId;

class Match : public servercore::job::JobQueue {
public:
    Match(uint64_t id, std::unique_ptr<MatchHandler> handler,
          MatchConfig config, NetDispatcher dispatcher,
          PlayerRegistry& registry, servercore::job::GlobalQueue& gq,
          servercore::buffer::BufferPool& pool);
    ~Match();

    uint64_t Id() const { return id_; }
    const MatchConfig& Config() const { return config_; }
    const std::string& Label() const { return config_.label; }
    void SetLabel(std::string label) { config_.label = std::move(label); }
    std::size_t PlayerCount() const { return players_.size(); }
    bool IsActive() const { return active_; }

    // Thread-safe -- Push jobs for serialized execution
    void RequestJoin(PlayerId pid, ContextId reactor);
    void RequestLeave(PlayerId pid);
    void DispatchMessage(PlayerId sender, int64_t opcode,
                         std::vector<std::byte> data);
    void Terminate();

    // Called from MatchHandler (match thread only)
    void SendToPlayer(PlayerId pid, int64_t opcode,
                      std::span<const std::byte> data);
    void BroadcastAll(int64_t opcode, std::span<const std::byte> data);
    void BroadcastExcept(PlayerId exclude, int64_t opcode,
                         std::span<const std::byte> data);

    void ScheduleTick(servercore::job::JobTimer& timer);

private:
    void HandleJoin(PlayerId pid, ContextId reactor);
    void HandleLeave(PlayerId pid);
    void HandleMessage(PlayerId sender, int64_t opcode,
                       std::vector<std::byte> data);
    void HandleTick(servercore::job::JobTimer& timer);
    void HandleTerminate();

    void SendToReactor(ContextId reactor, PlayerId pid, int64_t opcode,
                     std::span<const std::byte> data);

    uint64_t id_;
    MatchConfig config_;
    std::unique_ptr<MatchHandler> handler_;
    std::unordered_map<PlayerId, ContextId> players_;
    NetDispatcher net_dispatcher_;
    PlayerRegistry& registry_;
    servercore::buffer::BufferPool& pool_;
    bool active_ = true;
    std::chrono::steady_clock::time_point last_tick_;
};

} // namespace servergame::match
