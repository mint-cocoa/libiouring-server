#include <servergame/MatchRegistry.h>

using namespace servercore;

namespace servergame::match {

MatchRegistry::MatchRegistry(job::GlobalQueue& gq, NetDispatcher dispatcher,
                             PlayerRegistry& registry, buffer::BufferPool& pool)
    : gq_(gq), net_dispatcher_(std::move(dispatcher))
    , registry_(registry), pool_(pool) {}

void MatchRegistry::RegisterHandler(const std::string& name, HandlerFactory factory) {
    std::unique_lock lk(mutex_);
    factories_[name] = std::move(factory);
}

std::expected<Match*, GameError> MatchRegistry::CreateMatch(
        const std::string& handler_name, MatchConfig config,
        job::JobTimer& timer) {
    std::unique_ptr<MatchHandler> handler;
    {
        std::shared_lock lk(mutex_);
        auto it = factories_.find(handler_name);
        if (it == factories_.end())
            return std::unexpected(GameError::kMatchNotFound);
        handler = it->second();
    }

    auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
    config.handler_name = handler_name;

    auto match = std::make_shared<Match>(
        id, std::move(handler), std::move(config),
        net_dispatcher_, registry_, gq_, pool_);

    if (match->Config().tick_rate > 0)
        match->ScheduleTick(timer);

    auto* ptr = match.get();
    std::unique_lock lk(mutex_);
    matches_[id] = std::move(match);
    return ptr;
}

std::optional<Match*> MatchRegistry::FindMatch(uint64_t match_id) {
    std::shared_lock lk(mutex_);
    auto it = matches_.find(match_id);
    if (it != matches_.end())
        return it->second.get();
    return std::nullopt;
}

void MatchRegistry::RemoveMatch(uint64_t match_id) {
    std::unique_lock lk(mutex_);
    matches_.erase(match_id);
}

std::vector<uint64_t> MatchRegistry::ListMatches(const std::string& label) {
    std::shared_lock lk(mutex_);
    std::vector<uint64_t> result;
    for (auto& [id, match] : matches_) {
        if (label.empty() || match->Label() == label)
            result.push_back(id);
    }
    return result;
}

std::size_t MatchRegistry::ActiveCount() const {
    std::shared_lock lk(mutex_);
    return matches_.size();
}

} // namespace servergame::match
