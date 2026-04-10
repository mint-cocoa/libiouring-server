#include <servergame/PresenceTracker.h>
#include <servergame/WsFrameBuilder.h>

#include <nlohmann/json.hpp>

using namespace servercore;

namespace servergame::presence {

PresenceTracker::PresenceTracker(PlayerRegistry& registry, NetDispatcher dispatcher,
                                 buffer::BufferPool* pool)
    : registry_(registry), net_dispatcher_(std::move(dispatcher)), pool_(pool) {}

void PresenceTracker::OnPlayerConnected(PlayerId pid, ContextId reactor) {
    registry_.Register(pid, reactor);
}

void PresenceTracker::OnPlayerDisconnected(PlayerId pid) {
    UnsubscribeAll(pid);
    registry_.Unregister(pid);
}

void PresenceTracker::UpdateStatus(PlayerId pid, const std::string& status,
                                   const std::string& metadata) {
    registry_.UpdateStatus(pid, status);
}

void PresenceTracker::Subscribe(PlayerId subscriber, const std::string& topic) {
    std::unique_lock lk(sub_mutex_);
    topic_subscribers_[topic].insert(subscriber);
    player_topics_[subscriber].insert(topic);
}

void PresenceTracker::Unsubscribe(PlayerId subscriber, const std::string& topic) {
    std::unique_lock lk(sub_mutex_);
    if (auto it = topic_subscribers_.find(topic); it != topic_subscribers_.end()) {
        it->second.erase(subscriber);
        if (it->second.empty()) topic_subscribers_.erase(it);
    }
    if (auto it = player_topics_.find(subscriber); it != player_topics_.end()) {
        it->second.erase(topic);
        if (it->second.empty()) player_topics_.erase(it);
    }
}

void PresenceTracker::UnsubscribeAll(PlayerId subscriber) {
    std::unique_lock lk(sub_mutex_);
    if (auto pit = player_topics_.find(subscriber); pit != player_topics_.end()) {
        for (auto& topic : pit->second) {
            if (auto tit = topic_subscribers_.find(topic); tit != topic_subscribers_.end()) {
                tit->second.erase(subscriber);
                if (tit->second.empty()) topic_subscribers_.erase(tit);
            }
        }
        player_topics_.erase(pit);
    }
}

std::vector<PlayerId> PresenceTracker::GetSubscribers(const std::string& topic) const {
    std::shared_lock lk(sub_mutex_);
    if (auto it = topic_subscribers_.find(topic); it != topic_subscribers_.end())
        return {it->second.begin(), it->second.end()};
    return {};
}

void PresenceTracker::BroadcastEvent(const std::string& topic, const PresenceEvent& event) {
    std::vector<PlayerId> subscribers;
    {
        std::shared_lock lk(sub_mutex_);
        if (auto it = topic_subscribers_.find(topic); it != topic_subscribers_.end())
            subscribers.assign(it->second.begin(), it->second.end());
    }
    for (PlayerId pid : subscribers)
        DispatchToPlayer(pid, event);
}

void PresenceTracker::DispatchToPlayer(PlayerId pid, const PresenceEvent& event) {
    auto entry = registry_.Find(pid);
    if (!entry) return;

    if (pool_) {
        nlohmann::json j;
        j["type"] = "presence.event";
        j["player_id"] = event.player_id;
        j["status"] = event.status;
        switch (event.type) {
            case PresenceEventType::kJoin:   j["event"] = "join"; break;
            case PresenceEventType::kLeave:  j["event"] = "leave"; break;
            case PresenceEventType::kUpdate: j["event"] = "update"; break;
        }
        if (!event.metadata.empty()) j["metadata"] = event.metadata;

        auto buf = BuildWsTextFrame(*pool_, j.dump());
        if (buf) {
            net_dispatcher_(entry->context_id,
                           net::SendToPlayerCmd{pid, std::move(buf)});
        }
    } else {
        net_dispatcher_(entry->context_id,
                       net::SendToPlayerCmd{pid, nullptr});
    }
}

} // namespace servergame::presence
