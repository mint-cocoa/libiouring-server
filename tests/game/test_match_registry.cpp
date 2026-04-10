#include <servergame/MatchRegistry.h>
#include <servergame/Match.h>
#include <servergame/MatchHandler.h>
#include <servergame/MatchConfig.h>
#include <servergame/PlayerRegistry.h>

#include <servercore/job/GlobalQueue.h>
#include <servercore/job/JobTimer.h>

#include <gtest/gtest.h>

using namespace servercore;
using namespace servergame;
using namespace servergame::match;

class StubHandler : public MatchHandler {
public:
    void Init(Match&) override {}
    void OnJoin(Match&, PlayerId) override {}
    void OnLeave(Match&, PlayerId) override {}
    void OnMessage(Match&, PlayerId, int64_t, std::span<const std::byte>) override {}
    void OnTick(Match&, std::chrono::milliseconds) override {}
};

class MatchRegistryTest : public ::testing::Test {
protected:
    job::GlobalQueue gq;
    job::JobTimer timer;
    PlayerRegistry registry;
    buffer::BufferPool pool;

    NetDispatcher noop_dispatcher() {
        return [](ContextId, net::IoCommand) {};
    }
};

TEST_F(MatchRegistryTest, RegisterHandler) {
    MatchRegistry reg(gq, noop_dispatcher(), registry, pool);
    reg.RegisterHandler("test", [] { return std::make_unique<StubHandler>(); });
}

TEST_F(MatchRegistryTest, CreateAndFindMatch) {
    MatchRegistry reg(gq, noop_dispatcher(), registry, pool);
    reg.RegisterHandler("test", [] { return std::make_unique<StubHandler>(); });

    MatchConfig cfg{.tick_rate = 0, .handler_name = "test", .label = "room1"};
    auto result = reg.CreateMatch("test", cfg, timer);
    ASSERT_TRUE(result.has_value());
    auto* match = *result;
    ASSERT_NE(match, nullptr);

    auto found = reg.FindMatch(match->Id());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ((*found)->Id(), match->Id());
    EXPECT_EQ((*found)->Label(), "room1");
}

TEST_F(MatchRegistryTest, RemoveMatch) {
    MatchRegistry reg(gq, noop_dispatcher(), registry, pool);
    reg.RegisterHandler("test", [] { return std::make_unique<StubHandler>(); });

    MatchConfig cfg{.tick_rate = 0, .handler_name = "test"};
    auto result = reg.CreateMatch("test", cfg, timer);
    ASSERT_TRUE(result.has_value());
    auto id = (*result)->Id();
    reg.RemoveMatch(id);
    EXPECT_FALSE(reg.FindMatch(id).has_value());
    EXPECT_EQ(reg.ActiveCount(), 0u);
}

TEST_F(MatchRegistryTest, ListMatchesByLabel) {
    MatchRegistry reg(gq, noop_dispatcher(), registry, pool);
    reg.RegisterHandler("test", [] { return std::make_unique<StubHandler>(); });

    reg.CreateMatch("test", {.tick_rate = 0, .handler_name = "test", .label = "pvp"}, timer);
    reg.CreateMatch("test", {.tick_rate = 0, .handler_name = "test", .label = "pve"}, timer);
    reg.CreateMatch("test", {.tick_rate = 0, .handler_name = "test", .label = "pvp"}, timer);

    auto pvp = reg.ListMatches("pvp");
    EXPECT_EQ(pvp.size(), 2u);

    auto all = reg.ListMatches();
    EXPECT_EQ(all.size(), 3u);
}

TEST_F(MatchRegistryTest, ActiveCount) {
    MatchRegistry reg(gq, noop_dispatcher(), registry, pool);
    reg.RegisterHandler("test", [] { return std::make_unique<StubHandler>(); });

    EXPECT_EQ(reg.ActiveCount(), 0u);
    reg.CreateMatch("test", {.tick_rate = 0, .handler_name = "test"}, timer);
    EXPECT_EQ(reg.ActiveCount(), 1u);
}
