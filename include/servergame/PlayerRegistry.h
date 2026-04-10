#pragma once

#include <servercore/Types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace servergame {

using servercore::PlayerId;
using servercore::ContextId;

class PlayerRegistry {
public:
    struct Entry {
        ContextId context_id;
        std::string status = "online";
        uint64_t match_id = 0;
    };

    void Register(PlayerId pid, ContextId reactor);
    void Unregister(PlayerId pid);
    void UpdateStatus(PlayerId pid, const std::string& status);
    void UpdateMatch(PlayerId pid, uint64_t match_id);
    std::optional<Entry> Find(PlayerId pid) const;
    bool IsOnline(PlayerId pid) const;
    std::vector<PlayerId> GetOnlinePlayers() const;
    std::size_t OnlineCount() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<PlayerId, Entry> players_;
};

} // namespace servergame
