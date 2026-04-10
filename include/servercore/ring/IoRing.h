#pragma once

#include <servercore/Noncopyable.h>
#include <servercore/Profiler.h>
#include <servercore/Error.h>
#include <servercore/ring/RingBuffer.h>
#include <servercore/ring/RingEvent.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <sys/socket.h>
#include <sys/uio.h>

struct io_uring;
struct io_uring_sqe;

namespace servercore::ring {

struct IoRingConfig {
    std::size_t queue_depth{2048};
    RingBufferConfig buf_ring{};

    bool single_issuer{false};           // IORING_SETUP_SINGLE_ISSUER
    bool sqpoll{false};                  // IORING_SETUP_SQPOLL
    std::uint32_t sq_thread_idle{1000};  // SQPOLL idle timeout (ms)
    int sq_thread_cpu{-1};               // SQPOLL CPU affinity (-1 = none)
};

// RAII wrapper around Linux io_uring.
//
// Provides:
//  - CQE dispatch loop (Dispatch)
//  - SQE preparation helpers (PrepRecv, PrepSend, PrepAccept, ...)
//  - Cross-thread task posting (RunOnRing, Post)
//  - Provided buffer ring management
class IoRing : Noncopyable {
public:
    static std::expected<std::unique_ptr<IoRing>, RingError> Create(const IoRingConfig& cfg = {});
    ~IoRing();

    // ── CQE dispatch ─────────────────────────────────────────
    bool Dispatch(std::chrono::milliseconds timeout);

    // ── SQE preparation ──────────────────────────────────────
    io_uring_sqe* GetSqe();
    int Submit();

    [[nodiscard]] bool PrepRecv(RecvEvent& ev, int fd);
    [[nodiscard]] bool PrepRecvMultishot(RecvEvent& ev, int fd);
    [[nodiscard]] bool PrepSendMsg(SendEvent& ev, int fd, struct msghdr* msg, unsigned flags);
    [[nodiscard]] bool PrepAcceptMultishot(AcceptEvent& ev, int listen_fd);
    [[nodiscard]] bool PrepDisconnect(DisconnectEvent& ev, int fd);
    bool PrepCancel(IoEvent& target_ev);
    [[nodiscard]] bool PrepConnect(ConnectEvent& ev, int fd, const struct sockaddr* addr, socklen_t len);
    [[nodiscard]] bool PrepPollAdd(PollEvent& ev, int fd, unsigned poll_mask);
    bool PrepPollRemove(PollEvent& ev);

    // ── Cross-thread dispatch ────────────────────────────────
    bool RunOnRing(std::move_only_function<void()> task) noexcept;

    void Post(std::move_only_function<void()> task);
    void ProcessPostedTasks();

    // ── Accessors ────────────────────────────────────────────
    io_uring* Raw() const noexcept { return ring_; }
    int Fd() const noexcept;
    RingBuffer& BufRing() noexcept { return *buf_ring_; }

    static IoRing* Current() noexcept;
    static void SetCurrent(IoRing* ring) noexcept;

private:
    struct Deleter {
        void operator()(io_uring* r) const noexcept;
    };

    explicit IoRing(io_uring* ring, std::unique_ptr<RingBuffer> br);

    io_uring* ring_;
    std::unique_ptr<RingBuffer> buf_ring_;

    TracyLockable(std::mutex, post_mutex_);
    std::vector<std::move_only_function<void()>> posted_;

    static thread_local IoRing* t_current_;
};

} // namespace servercore::ring
