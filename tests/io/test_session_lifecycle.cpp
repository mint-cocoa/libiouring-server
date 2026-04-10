#include <servercore/io/Session.h>
#include <servercore/ring/IoRing.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

using namespace servercore;
using namespace servercore::io;
using namespace servercore::ring;
using namespace servercore::buffer;

// -- Test subclass ----

class TestSession : public Session {
public:
    using Session::Session;

    std::atomic<bool> connected{false};
    std::atomic<bool> disconnected{false};
    std::atomic<int> disconnect_count{0};
    std::atomic<int> recv_count{0};
    std::atomic<int> recv_bytes{0};

protected:
    void OnRecv(std::span<const std::byte> data) override {
        recv_count.fetch_add(1, std::memory_order_relaxed);
        recv_bytes.fetch_add(static_cast<int>(data.size()), std::memory_order_relaxed);
    }

    void OnConnected() override {
        connected.store(true, std::memory_order_relaxed);
    }

    void OnDisconnected() override {
        disconnected.store(true, std::memory_order_relaxed);
        disconnect_count.fetch_add(1, std::memory_order_relaxed);
    }
};

// -- Helpers ----

static constexpr IoRingConfig kTestRingConfig{
    .queue_depth = 64,
    .buf_ring = {.buf_count = 16, .buf_size = 4096},
};

// Dispatch loop with a deadline.
static void DispatchUntil(IoRing& ring, std::function<bool()> pred,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds{2000}) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        ring.ProcessPostedTasks();
        ring.Dispatch(std::chrono::milliseconds{10});
    }
}

struct SocketPair {
    int local;   // Session side
    int remote;  // Peer side
};

static SocketPair MakeSocketPair() {
    int sv[2];
    int ret = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv);
    EXPECT_EQ(ret, 0);
    return {sv[0], sv[1]};
}

// -- Fixture ----

class SessionLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto result = IoRing::Create(kTestRingConfig);
        ASSERT_TRUE(result.has_value());
        ring_ = std::move(*result);
        IoRing::SetCurrent(ring_.get());
    }

    void TearDown() override {
        IoRing::SetCurrent(nullptr);
    }

    BufferPool pool_;
    std::unique_ptr<IoRing> ring_;
};

// -- Tests ----

// Peer close -> recv EOF -> Disconnect -> TryRelease -> self_ref released,
// disconnect callback called exactly once.
TEST_F(SessionLifecycleTest, NormalDisconnect) {
    auto [local_fd, remote_fd] = MakeSocketPair();

    auto sess = std::make_shared<TestSession>(local_fd, *ring_, pool_);
    sess->Start();

    EXPECT_TRUE(sess->connected.load());
    EXPECT_FALSE(sess->disconnected.load());

    // Close peer -> triggers recv EOF on local
    ::close(remote_fd);

    DispatchUntil(*ring_, [&] { return sess->disconnected.load(); });

    EXPECT_TRUE(sess->disconnected.load());
    EXPECT_EQ(sess->disconnect_count.load(), 1);

    // After TryRelease, only our local shared_ptr should remain
    // (self_ref_ was reset inside TryRelease)
    EXPECT_EQ(sess.use_count(), 1);
}

// Send data, then peer closes. Both send CQE and recv EOF must settle
// before release.
TEST_F(SessionLifecycleTest, SendThenPeerClose) {
    auto [local_fd, remote_fd] = MakeSocketPair();

    auto sess = std::make_shared<TestSession>(local_fd, *ring_, pool_);
    sess->Start();

    // Send some data
    auto buf_result = pool_.Allocate(128);
    ASSERT_TRUE(buf_result.has_value());
    auto buf = std::move(*buf_result);
    std::memset(buf->Writable().data(), 0xAB, 128);
    buf->Commit(128);
    sess->Send(std::move(buf));

    // Let send complete
    ring_->Dispatch(std::chrono::milliseconds{50});

    // Read from remote to ensure send completes
    std::byte tmp[256];
    ::recv(remote_fd, tmp, sizeof(tmp), 0);

    // Now close peer
    ::close(remote_fd);

    DispatchUntil(*ring_, [&] { return sess->disconnected.load(); });

    EXPECT_TRUE(sess->disconnected.load());
    EXPECT_EQ(sess->disconnect_count.load(), 1);
    EXPECT_EQ(sess.use_count(), 1);
}

// When send queue overflows, the overflow callback fires and
// Session auto-disconnects.
TEST_F(SessionLifecycleTest, SendQueueOverflow) {
    auto [local_fd, remote_fd] = MakeSocketPair();

    auto sess = std::make_shared<TestSession>(local_fd, *ring_, pool_);

    std::atomic<bool> overflow_fired{false};
    sess->SetSendOverflowCallback([&](SessionRef) {
        overflow_fired.store(true, std::memory_order_relaxed);
    });

    sess->Start();

    // Fill the send queue (default max_pending = 4096) beyond capacity.
    // Each Push must return overflow at some point.
    // We need to push faster than the IO thread can drain.
    // The default SendQueue max is 4096, so push slightly above that.
    for (int i = 0; i < 5000; ++i) {
        auto buf_result = pool_.Allocate(64);
        if (!buf_result) break;
        auto buf = std::move(*buf_result);
        std::memset(buf->Writable().data(), 0, 64);
        buf->Commit(64);
        sess->Send(std::move(buf));
        if (overflow_fired.load(std::memory_order_relaxed)) break;
    }

    // Dispatch to let disconnect settle
    DispatchUntil(*ring_, [&] { return sess->disconnected.load(); });

    EXPECT_TRUE(overflow_fired.load());
    EXPECT_TRUE(sess->Disconnecting());
    EXPECT_TRUE(sess->disconnected.load());

    // Cleanup peer side
    ::close(remote_fd);
}

// External shared_ptr release should not destroy Session while
// self_ref_ keeps it alive during active I/O. Session is only
// destroyed after all CQEs settle.
TEST_F(SessionLifecycleTest, SelfRefRelease) {
    auto [local_fd, remote_fd] = MakeSocketPair();

    std::weak_ptr<TestSession> weak;
    {
        auto sess = std::make_shared<TestSession>(local_fd, *ring_, pool_);
        weak = sess;
        sess->Start();
        EXPECT_TRUE(sess->connected.load());

        // sess goes out of scope — only self_ref_ and weak remain
    }

    // Session must still be alive (self_ref_ holds it)
    ASSERT_FALSE(weak.expired());

    // Close peer to trigger disconnect path
    ::close(remote_fd);

    DispatchUntil(*ring_, [&] { return weak.expired(); });

    // After all CQEs settle and TryRelease runs, self_ref_ is released
    // and the weak_ptr expires.
    EXPECT_TRUE(weak.expired());
}
