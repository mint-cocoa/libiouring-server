#include <servergame/PlayerRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <thread>
#include <vector>

using namespace servergame;

class PlayerRegistryTest : public ::testing::Test {
protected:
    PlayerRegistry registry;
};

TEST_F(PlayerRegistryTest, RegisterAndFind) {
    registry.Register(1, 0);
    auto entry = registry.Find(1);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->context_id, 0);
    EXPECT_EQ(entry->status, "online");
    EXPECT_EQ(entry->match_id, 0u);
}

TEST_F(PlayerRegistryTest, FindUnregistered) {
    EXPECT_FALSE(registry.Find(999).has_value());
}

TEST_F(PlayerRegistryTest, Unregister) {
    registry.Register(1, 0);
    EXPECT_TRUE(registry.IsOnline(1));
    registry.Unregister(1);
    EXPECT_FALSE(registry.IsOnline(1));
}

TEST_F(PlayerRegistryTest, UpdateStatus) {
    registry.Register(1, 0);
    registry.UpdateStatus(1, "in_game");
    auto entry = registry.Find(1);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->status, "in_game");
}

TEST_F(PlayerRegistryTest, UpdateMatch) {
    registry.Register(1, 0);
    registry.UpdateMatch(1, 42);
    auto entry = registry.Find(1);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->match_id, 42u);
}

TEST_F(PlayerRegistryTest, OnlineCount) {
    EXPECT_EQ(registry.OnlineCount(), 0u);
    registry.Register(1, 0);
    registry.Register(2, 1);
    EXPECT_EQ(registry.OnlineCount(), 2u);
    registry.Unregister(1);
    EXPECT_EQ(registry.OnlineCount(), 1u);
}

TEST_F(PlayerRegistryTest, GetOnlinePlayers) {
    registry.Register(10, 0);
    registry.Register(20, 1);
    auto players = registry.GetOnlinePlayers();
    EXPECT_EQ(players.size(), 2u);
    EXPECT_NE(std::find(players.begin(), players.end(), 10), players.end());
    EXPECT_NE(std::find(players.begin(), players.end(), 20), players.end());
}

TEST_F(PlayerRegistryTest, ConcurrentAccess) {
    constexpr int kThreads = 4;
    constexpr int kOps = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kOps; ++i) {
                PlayerId pid = t * kOps + i;
                registry.Register(pid, static_cast<ContextId>(t));
                registry.IsOnline(pid);
                registry.UpdateStatus(pid, "active");
                registry.Find(pid);
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(registry.OnlineCount(), static_cast<std::size_t>(kThreads * kOps));
}
