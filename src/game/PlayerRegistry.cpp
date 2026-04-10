#include <servergame/PlayerRegistry.h>

namespace servergame {

void PlayerRegistry::Register(PlayerId pid, ContextId reactor) {
    std::unique_lock lk(mutex_);
    players_[pid] = Entry{.context_id = reactor, .status = "online", .match_id = 0};
}

void PlayerRegistry::Unregister(PlayerId pid) {
    std::unique_lock lk(mutex_);
    players_.erase(pid);
}

void PlayerRegistry::UpdateStatus(PlayerId pid, const std::string& status) {
    std::unique_lock lk(mutex_);
    if (auto it = players_.find(pid); it != players_.end())
        it->second.status = status;
}

void PlayerRegistry::UpdateMatch(PlayerId pid, uint64_t match_id) {
    std::unique_lock lk(mutex_);
    if (auto it = players_.find(pid); it != players_.end())
        it->second.match_id = match_id;
}

std::optional<PlayerRegistry::Entry> PlayerRegistry::Find(PlayerId pid) const {
    std::shared_lock lk(mutex_);
    if (auto it = players_.find(pid); it != players_.end())
        return it->second;
    return std::nullopt;
}

bool PlayerRegistry::IsOnline(PlayerId pid) const {
    std::shared_lock lk(mutex_);
    return players_.contains(pid);
}

std::vector<PlayerId> PlayerRegistry::GetOnlinePlayers() const {
    std::shared_lock lk(mutex_);
    std::vector<PlayerId> result;
    result.reserve(players_.size());
    for (auto& [pid, _] : players_)
        result.push_back(pid);
    return result;
}

std::size_t PlayerRegistry::OnlineCount() const {
    std::shared_lock lk(mutex_);
    return players_.size();
}

} // namespace servergame
