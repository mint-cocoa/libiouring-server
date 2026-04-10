#include <servergame/MatchmakerQueue.h>
#include <servergame/MatchmakerTicket.h>
#include <servergame/MatchRegistry.h>
#include <servergame/MatchHandler.h>
#include <servergame/PlayerRegistry.h>

#include <servercore/job/GlobalQueue.h>
#include <servercore/job/JobTimer.h>

#include <gtest/gtest.h>

using namespace servercore;
using namespace servergame;
using namespace servergame::matchmaker;
using namespace servergame::match;

class StubHandler2 : public MatchHandler {
    void Init(Match&) override {}
    void OnJoin(Match&, PlayerId) override {}
    void OnLeave(Match&, PlayerId) override {}
    void OnMessage(Match&, PlayerId, int64_t, std::span<const std::byte>) override {}
    void OnTick(Match&, std::chrono::milliseconds) override {}
};

class MatchmakerQueueTest : public ::testing::Test {
protected:
    job::GlobalQueue gq;
    job::JobTimer timer;
    PlayerRegistry registry;
    buffer::BufferPool pool;
    std::vector<std::pair<ContextId, net::IoCommand>> dispatched;

    NetDispatcher make_dispatcher() {
        return [this](ContextId sid, net::IoCommand cmd) {
            dispatched.emplace_back(sid, std::move(cmd));
        };
    }
};

TEST_F(MatchmakerQueueTest, AddAndRemoveTicket) {
    MatchRegistry match_reg(gq, make_dispatcher(), registry, pool);
    MatchmakerQueue mm(gq, match_reg, registry, make_dispatcher(), pool);

    MatchmakerTicket t1;
    t1.player_id = 1;
    t1.min_count = 2;
    t1.max_count = 2;
    mm.AddTicket(std::move(t1));
    mm.Execute();

    EXPECT_EQ(mm.TicketCount(), 1u);

    mm.RemoveByPlayer(1);
    mm.Execute();
    EXPECT_EQ(mm.TicketCount(), 0u);
}

TEST_F(MatchmakerQueueTest, TwoCompatiblePlayersMatch) {
    MatchRegistry match_reg(gq, make_dispatcher(), registry, pool);
    match_reg.RegisterHandler("test", [] { return std::make_unique<StubHandler2>(); });

    MatchmakerQueue mm(gq, match_reg, registry, make_dispatcher(), pool);

    registry.Register(1, 0);
    registry.Register(2, 1);

    MatchmakerTicket t1;
    t1.player_id = 1;
    t1.context_id = 0;
    t1.min_count = 2;
    t1.max_count = 2;
    t1.query = "+region:asia";
    t1.handler_name = "test";
    t1.string_props = {{"region", "asia"}};
    t1.submitted_at = std::chrono::steady_clock::now();

    MatchmakerTicket t2;
    t2.player_id = 2;
    t2.context_id = 1;
    t2.min_count = 2;
    t2.max_count = 2;
    t2.query = "+region:asia";
    t2.handler_name = "test";
    t2.string_props = {{"region", "asia"}};
    t2.submitted_at = std::chrono::steady_clock::now();

    mm.AddTicket(std::move(t1));
    mm.Execute();
    mm.AddTicket(std::move(t2));
    mm.Execute();

    mm.ProcessTickNow(timer);
    mm.Execute();

    EXPECT_EQ(mm.TicketCount(), 0u);
    EXPECT_EQ(match_reg.ActiveCount(), 1u);
}

TEST_F(MatchmakerQueueTest, IncompatiblePlayersNoMatch) {
    MatchRegistry match_reg(gq, make_dispatcher(), registry, pool);
    match_reg.RegisterHandler("test", [] { return std::make_unique<StubHandler2>(); });

    MatchmakerQueue mm(gq, match_reg, registry, make_dispatcher(), pool);

    registry.Register(1, 0);
    registry.Register(2, 1);

    MatchmakerTicket t1;
    t1.player_id = 1;
    t1.min_count = 2;
    t1.max_count = 2;
    t1.query = "+region:asia";
    t1.handler_name = "test";
    t1.string_props = {{"region", "asia"}};
    t1.submitted_at = std::chrono::steady_clock::now();

    MatchmakerTicket t2;
    t2.player_id = 2;
    t2.min_count = 2;
    t2.max_count = 2;
    t2.query = "+region:eu";
    t2.handler_name = "test";
    t2.string_props = {{"region", "eu"}};
    t2.submitted_at = std::chrono::steady_clock::now();

    mm.AddTicket(std::move(t1));
    mm.Execute();
    mm.AddTicket(std::move(t2));
    mm.Execute();

    mm.ProcessTickNow(timer);
    mm.Execute();

    EXPECT_EQ(mm.TicketCount(), 2u);
    EXPECT_EQ(match_reg.ActiveCount(), 0u);
}

TEST_F(MatchmakerQueueTest, TimeoutRemovesTicket) {
    MatchRegistry match_reg(gq, make_dispatcher(), registry, pool);
    MatchmakerQueue mm(gq, match_reg, registry, make_dispatcher(), pool);

    registry.Register(1, 0);

    MatchmakerTicket t1;
    t1.player_id = 1;
    t1.context_id = 0;
    t1.min_count = 2;
    t1.max_count = 2;
    t1.handler_name = "test";
    t1.submitted_at = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    t1.timeout = std::chrono::seconds{30};

    mm.AddTicket(std::move(t1));
    mm.Execute();

    mm.ProcessTickNow(timer);
    mm.Execute();

    EXPECT_EQ(mm.TicketCount(), 0u);
}
