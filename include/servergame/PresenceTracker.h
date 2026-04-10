#pragma once

#include <servergame/NetDispatchFn.h>
#include <servergame/PlayerRegistry.h>
#include <servergame/PresenceEvent.h>
#include <servercore/buffer/SendBuffer.h>

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace servergame::presence {

using servercore::PlayerId;
using servercore::ContextId;

class PresenceTracker {
public:
    /// @param pool Optional -- if null, BroadcastEvent dispatches NetCommands with empty packet.
    PresenceTracker(PlayerRegistry& registry, NetDispatcher dispatcher,
                    servercore::buffer::BufferPool* pool = nullptr);

    void OnPlayerConnected(PlayerId pid, ContextId reactor);
    void OnPlayerDisconnected(PlayerId pid);
    void UpdateStatus(PlayerId pid, const std::string& status,
                      const std::string& metadata = "");

    void Subscribe(PlayerId subscriber, const std::string& topic);
    void Unsubscribe(PlayerId subscriber, const std::string& topic);
    void UnsubscribeAll(PlayerId subscriber);

    std::vector<PlayerId> GetSubscribers(const std::string& topic) const;
    void BroadcastEvent(const std::string& topic, const PresenceEvent& event);

private:
    void DispatchToPlayer(PlayerId pid, const PresenceEvent& event);

    PlayerRegistry& registry_;
    NetDispatcher net_dispatcher_;
    servercore::buffer::BufferPool* pool_;

    mutable std::shared_mutex sub_mutex_;
    std::unordered_map<std::string, std::unordered_set<PlayerId>> topic_subscribers_;
    std::unordered_map<PlayerId, std::unordered_set<std::string>> player_topics_;
};

} // namespace servergame::presence
