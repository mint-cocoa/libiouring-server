#pragma once

#include <servergame/NetDispatchFn.h>
#include <servergame/PlayerRegistry.h>
#include <servergame/Match.h>
#include <servergame/MatchConfig.h>
#include <servergame/MatchHandler.h>
#include <servergame/Error.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/job/GlobalQueue.h>
#include <servercore/job/JobTimer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace servergame::match {

class MatchRegistry {
public:
    using HandlerFactory = std::function<std::unique_ptr<MatchHandler>()>;

    MatchRegistry(servercore::job::GlobalQueue& gq, NetDispatcher dispatcher,
                  PlayerRegistry& registry, servercore::buffer::BufferPool& pool);

    void RegisterHandler(const std::string& name, HandlerFactory factory);

    std::expected<Match*, GameError> CreateMatch(const std::string& handler_name,
                                                 MatchConfig config,
                                                 servercore::job::JobTimer& timer);
    std::optional<Match*> FindMatch(uint64_t match_id);
    void RemoveMatch(uint64_t match_id);
    std::vector<uint64_t> ListMatches(const std::string& label = "");
    std::size_t ActiveCount() const;

private:
    servercore::job::GlobalQueue& gq_;
    NetDispatcher net_dispatcher_;
    PlayerRegistry& registry_;
    servercore::buffer::BufferPool& pool_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, HandlerFactory> factories_;
    std::unordered_map<uint64_t, std::shared_ptr<Match>> matches_;
    std::atomic<uint64_t> next_id_{1};
};

} // namespace servergame::match
