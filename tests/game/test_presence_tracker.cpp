#include <servergame/PresenceTracker.h>
#include <servergame/PresenceEvent.h>
#include <servergame/PlayerRegistry.h>

#include <gtest/gtest.h>

#include <mutex>
#include <vector>

using namespace servercore;
using namespace servergame;
using namespace servergame::presence;

class PresenceTrackerTest : public ::testing::Test {
protected:
    PlayerRegistry registry;
    std::mutex dispatch_mutex;
    std::vector<std::pair<ContextId, net::IoCommand>> dispatched;

    NetDispatcher make_dispatcher() {
        return [this](ContextId sid, net::IoCommand cmd) {
            std::lock_guard lk(dispatch_mutex);
            dispatched.emplace_back(sid, std::move(cmd));
        };
    }
};

TEST_F(PresenceTrackerTest, ConnectRegistersPlayer) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    EXPECT_TRUE(registry.IsOnline(1));
    auto entry = registry.Find(1);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->context_id, 0);
}

TEST_F(PresenceTrackerTest, DisconnectUnregistersPlayer) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    tracker.OnPlayerDisconnected(1);
    EXPECT_FALSE(registry.IsOnline(1));
}

TEST_F(PresenceTrackerTest, SubscribeAndGetSubscribers) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    tracker.OnPlayerConnected(2, 1);
    tracker.Subscribe(1, "match:100");
    tracker.Subscribe(2, "match:100");
    auto subs = tracker.GetSubscribers("match:100");
    EXPECT_EQ(subs.size(), 2u);
}

TEST_F(PresenceTrackerTest, Unsubscribe) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    tracker.Subscribe(1, "room:1");
    tracker.Unsubscribe(1, "room:1");
    EXPECT_TRUE(tracker.GetSubscribers("room:1").empty());
}

TEST_F(PresenceTrackerTest, UnsubscribeAllOnDisconnect) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    tracker.Subscribe(1, "topic:a");
    tracker.Subscribe(1, "topic:b");
    tracker.OnPlayerDisconnected(1);
    EXPECT_TRUE(tracker.GetSubscribers("topic:a").empty());
    EXPECT_TRUE(tracker.GetSubscribers("topic:b").empty());
}

TEST_F(PresenceTrackerTest, BroadcastEventDispatchesToSubscribers) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    tracker.OnPlayerConnected(2, 1);
    tracker.Subscribe(1, "room:1");
    tracker.Subscribe(2, "room:1");

    PresenceEvent event{PresenceEventType::kJoin, 3, "online", ""};
    tracker.BroadcastEvent("room:1", event);

    std::lock_guard lk(dispatch_mutex);
    EXPECT_EQ(dispatched.size(), 2u);
}

TEST_F(PresenceTrackerTest, UpdateStatusUpdatesRegistry) {
    PresenceTracker tracker(registry, make_dispatcher());
    tracker.OnPlayerConnected(1, 0);
    tracker.UpdateStatus(1, "away", "{}");
    auto entry = registry.Find(1);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->status, "away");
}
