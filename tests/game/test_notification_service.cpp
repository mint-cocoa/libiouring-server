#include <servergame/NotificationService.h>
#include <servergame/Notification.h>
#include <servergame/PlayerRegistry.h>

#include <gtest/gtest.h>

#include <mutex>
#include <vector>

using namespace servercore;
using namespace servergame;
using namespace servergame::notification;

class NotificationServiceTest : public ::testing::Test {
protected:
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

TEST_F(NotificationServiceTest, SendToOnlinePlayer) {
    NotificationService svc(registry, nullptr, make_dispatcher(), pool);

    registry.Register(42, 0);

    Notification n;
    n.recipient_id = 42;
    n.code = 1;
    n.subject = "Match Found";
    n.content = R"({"match_id": 123})";
    n.persistent = false;

    svc.Send(std::move(n));

    std::lock_guard lk(dispatch_mutex);
    EXPECT_EQ(dispatched.size(), 1u);
    EXPECT_EQ(dispatched[0].first, 0);
    EXPECT_EQ(std::get<net::SendToPlayerCmd>(dispatched[0].second).target, 42u);
}

TEST_F(NotificationServiceTest, SendToOfflineNonPersistent) {
    NotificationService svc(registry, nullptr, make_dispatcher(), pool);

    Notification n;
    n.recipient_id = 999;
    n.persistent = false;

    svc.Send(std::move(n));

    std::lock_guard lk(dispatch_mutex);
    EXPECT_TRUE(dispatched.empty());
}

TEST_F(NotificationServiceTest, SendMultiToOnlinePlayers) {
    NotificationService svc(registry, nullptr, make_dispatcher(), pool);

    registry.Register(1, 0);
    registry.Register(2, 1);
    registry.Register(3, 0);

    svc.SendMulti({1, 2, 3}, 1, "Hello", "{}", false);

    std::lock_guard lk(dispatch_mutex);
    EXPECT_EQ(dispatched.size(), 3u);
}
