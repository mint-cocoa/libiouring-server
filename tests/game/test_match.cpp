#include <servergame/Match.h>
#include <servergame/MatchHandler.h>
#include <servergame/MatchConfig.h>
#include <servergame/PlayerRegistry.h>

#include <servercore/job/GlobalQueue.h>
#include <servercore/job/JobTimer.h>

#include <gtest/gtest.h>

#include <mutex>
#include <vector>

using namespace servercore;
using namespace servergame;
using namespace servergame::match;

class TestHandler : public MatchHandler {
public:
    int init_count = 0;
    int join_count = 0;
    int leave_count = 0;
    int message_count = 0;
    int tick_count = 0;
    int terminate_count = 0;
    PlayerId last_joined = 0;
    bool reject_joins = false;

    void Init(Match& match) override { ++init_count; }
    bool JoinAttempt(Match& match, PlayerId pid) override { return !reject_joins; }
    void OnJoin(Match& match, PlayerId pid) override { ++join_count; last_joined = pid; }
    void OnLeave(Match& match, PlayerId pid) override { ++leave_count; }
    void OnMessage(Match& match, PlayerId sender,
                   int64_t opcode, std::span<const std::byte> data) override { ++message_count; }
    void OnTick(Match& match, std::chrono::milliseconds dt) override { ++tick_count; }
    void OnTerminate(Match& match) override { ++terminate_count; }
};

class MatchTest : public ::testing::Test {
protected:
    job::GlobalQueue gq;
    PlayerRegistry registry;
    buffer::BufferPool pool;
    std::mutex dispatch_mutex;
    std::vector<std::pair<ContextId, net::IoCommand>> dispatched;

    NetDispatcher make_dispatcher() {
        return [this](ContextId sid, net::IoCommand cmd) {
            std::lock_guard lk(dispatch_mutex);
            dispatched.emplace_back(sid, std::move(cmd));
        };
    }
};

TEST_F(MatchTest, InitCallsHandler) {
    auto handler = std::make_unique<TestHandler>();
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 0, .handler_name = "test"};
    Match match(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);
    EXPECT_EQ(h->init_count, 1);
    EXPECT_EQ(match.Id(), 1u);
    EXPECT_TRUE(match.IsActive());
}

TEST_F(MatchTest, JoinAndLeave) {
    auto handler = std::make_unique<TestHandler>();
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 0, .handler_name = "test"};
    Match match(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);

    registry.Register(42, 0);
    match.RequestJoin(42, 0);
    match.Execute();

    EXPECT_EQ(h->join_count, 1);
    EXPECT_EQ(h->last_joined, 42u);
    EXPECT_EQ(match.PlayerCount(), 1u);

    match.RequestLeave(42);
    match.Execute();
    EXPECT_EQ(h->leave_count, 1);
    EXPECT_EQ(match.PlayerCount(), 0u);
}

TEST_F(MatchTest, JoinRejected) {
    auto handler = std::make_unique<TestHandler>();
    handler->reject_joins = true;
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 0, .handler_name = "test"};
    Match match(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);

    registry.Register(42, 0);
    match.RequestJoin(42, 0);
    match.Execute();

    EXPECT_EQ(h->join_count, 0);
    EXPECT_EQ(match.PlayerCount(), 0u);
}

TEST_F(MatchTest, MaxPlayersEnforced) {
    auto handler = std::make_unique<TestHandler>();
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 0, .max_players = 1, .handler_name = "test"};
    Match match(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);

    registry.Register(1, 0);
    registry.Register(2, 1);

    match.RequestJoin(1, 0);
    match.Execute();
    EXPECT_EQ(h->join_count, 1);

    match.RequestJoin(2, 1);
    match.Execute();
    EXPECT_EQ(h->join_count, 1);
}

TEST_F(MatchTest, DispatchMessage) {
    auto handler = std::make_unique<TestHandler>();
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 0, .handler_name = "test"};
    Match match(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);

    std::vector<std::byte> data{std::byte{0x01}, std::byte{0x02}};
    match.DispatchMessage(42, 1, data);
    match.Execute();
    EXPECT_EQ(h->message_count, 1);
}

TEST_F(MatchTest, Terminate) {
    auto handler = std::make_unique<TestHandler>();
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 0, .handler_name = "test"};
    Match match(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);

    match.Terminate();
    match.Execute();
    EXPECT_EQ(h->terminate_count, 1);
    EXPECT_FALSE(match.IsActive());
}

TEST_F(MatchTest, TickViaJobTimer) {
    auto handler = std::make_unique<TestHandler>();
    auto* h = handler.get();
    MatchConfig cfg{.tick_rate = 20, .handler_name = "test"};
    job::JobTimer timer;
    auto match = std::make_shared<Match>(1, std::move(handler), cfg, make_dispatcher(), registry, gq, pool);

    match->ScheduleTick(timer);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    timer.DistributeExpired();
    match->Execute();

    EXPECT_GE(h->tick_count, 1);
}
