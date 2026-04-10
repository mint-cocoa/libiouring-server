#include <servercore/io/Session.h>
#include <servercore/ring/IoRing.h>
#include <servercore/Profiler.h>

#include <liburing.h>
#include <spdlog/spdlog.h>

#include <sys/socket.h>

namespace servercore::io {

Session::Session(int fd, IoRing& ring, BufferPool& pool)
    : socket_(fd)
    , ring_(ring)
    , pool_(pool) {}

Session::~Session() = default;

void Session::Start() {
    auto self = std::static_pointer_cast<Session>(shared_from_this());
    self_ref_ = self;  // self-ownership: Session keeps itself alive
    recv_ev_.SetOwner(self);
    send_ev_.SetOwner(self);
    disconnect_ev_.SetOwner(self);
    OnConnected();
    if (on_connected_)
        on_connected_(self);
    RegisterRecv();
}

std::expected<void, io::IoError> Session::Send(buffer::SendBufferRef buf) {
    if (disconnecting_) return std::unexpected(IoError::kDisconnected);

    auto result = send_queue_.Push(std::move(buf));
    if (result.overflowed) {
        spdlog::warn("Session[fd={}]: [DISC:SEND_OVERFLOW] send queue overflow", Fd());
        if (on_send_overflow_)
            on_send_overflow_(std::static_pointer_cast<Session>(shared_from_this()));
        Disconnect();
        return std::unexpected(IoError::kSendFailed);
    }
    if (result.needs_register) {
        RegisterSend();
    }
    return {};
}

void Session::Disconnect() {
    if (disconnecting_) return;
    disconnecting_ = true;
    spdlog::warn("Session[fd={} sid={}]: [DISC:ENTER] Disconnect() called", Fd(), session_id_);

    // 1. Shutdown the socket — causes recv to EOF and send to error,
    //    which naturally terminates the multishot recv.
    if (!ring_.PrepDisconnect(disconnect_ev_, Fd())) {
        // SQE full: cannot submit shutdown. Force-release to avoid leak.
        spdlog::error("Session[fd={}]: [DISC:SQE_FULL_SHUTDOWN] PrepDisconnect failed", Fd());
        pending_io_ = 0;
        TryRelease();
        return;
    }
    ++pending_io_;  // disconnect CQE pending

    // 2. Cancel multishot recv for faster cleanup (best-effort).
    //    If PrepCancel fails (SQE full), shutdown alone will still
    //    terminate the multishot by delivering EOF.
    ring_.PrepCancel(recv_ev_);

    ring_.Submit();
}

// -- Recv handling (fast/slow path + multishot + ENOBUFS) ----

void Session::OnRecv(ring::RecvEvent& ev,
                     std::int32_t res, std::uint32_t flags) {
    const bool more = (flags & IORING_CQE_F_MORE) != 0;
    const bool has_buffer = (flags & IORING_CQE_F_BUFFER) != 0;

    // No F_MORE means the kernel will send no more CQEs for this SQE.
    if (!more)
        --pending_io_;

    // Always return provided buffer to the ring, even during disconnect.
    // Failing to return leaks buffers from the provided buffer pool.
    if (has_buffer) {
        std::uint16_t buf_id = flags >> IORING_CQE_BUFFER_SHIFT;
        auto& buf_ring = ring_.BufRing();

        if (res > 0 && !disconnecting_) {
            auto view = buf_ring.View(buf_id, static_cast<std::uint32_t>(res));
            OnRecv(view);
        }

        buf_ring.Return(buf_id);
    }

    // During disconnect, skip normal error handling — just wait for
    // all in-flight ops to settle before releasing self_ref_.
    if (disconnecting_) {
        TryRelease();
        return;
    }

    // Normal error handling
    if (res == 0) {
        // Peer closed session
        spdlog::warn("Session[fd={}]: [DISC:PEER_CLOSE] peer closed connection", Fd());
        Disconnect();
        return;
    }

    if (res == -ENOBUFS) {
        // Buffer pool exhausted — multishot terminated, re-register
        spdlog::warn("Session[fd={}]: [WARN:ENOBUFS] provided buffer pool exhausted, re-registering", Fd());
        RegisterRecv();
        return;
    }

    if (res < 0) {
        spdlog::error("Session[fd={}]: [DISC:RECV_ERR] recv error res={}", Fd(), res);
        Disconnect();
        return;
    }

    // Multishot ended normally (e.g. internal resource limit) — re-register
    if (!more)
        RegisterRecv();
}

void Session::OnSend(ring::SendEvent& ev, std::int32_t res) {
    // Release in-flight buffer references.
    // Note: partial sendmsg (res < requested) is not handled — io_uring sendmsg
    // delivers either the full message or an error for stream sockets.
    in_flight_bufs_.clear();
    send_iovecs_.clear();
    --pending_io_;

    if (disconnecting_) {
        TryRelease();
        return;
    }

    if (res < 0) {
        spdlog::error("Session[fd={}]: [DISC:SEND_ERR] send error res={}", Fd(), res);
        Disconnect();
        return;
    }

    // Check if more data queued while we were sending
    send_queue_.MarkSent();
    auto pending = send_queue_.Drain();
    if (!pending.empty()) {
        SendBatch(std::move(pending));
    }
}

void Session::OnDisconnect(ring::DisconnectEvent& ev,
                           std::int32_t res) {
    --pending_io_;
    TryRelease();
}

// -- Self-ownership release gate ----

void Session::TryRelease() {
    if (pending_io_ > 0)
        return;

    spdlog::warn("Session[fd={} sid={}]: [DISC:RELEASED] session destroyed", Fd(), session_id_);
    OnDisconnected();
    if (on_disconnect_)
        on_disconnect_(std::static_pointer_cast<Session>(shared_from_this()));

    // Release self-ownership. The Dispatch keep_alive vector still holds a
    // reference, so actual destruction is deferred until the CQE batch ends.
    self_ref_.reset();
}

void Session::ReleaseOwnership() {
    self_ref_.reset();
}

// -- SQE registration ----

void Session::RegisterRecv() {
    if (disconnecting_) return;
    ++pending_io_;
    if (!ring_.PrepRecvMultishot(recv_ev_, Fd())) {
        --pending_io_;
        spdlog::error("Session[fd={}]: [DISC:SQE_FULL_RECV] PrepRecvMultishot failed", Fd());
        Disconnect();
        return;
    }
    ring_.Submit();
}

void Session::RegisterSend() {
    if (disconnecting_) return;

    auto bufs = send_queue_.Drain();
    if (bufs.empty()) return;

    SendBatch(std::move(bufs));
}

void Session::SendBatch(std::vector<SendBufferRef> bufs) {
    if (disconnecting_ || bufs.empty()) return;

    // Build iovec array from send buffers
    send_iovecs_.resize(bufs.size());
    for (std::size_t i = 0; i < bufs.size(); ++i) {
        auto data = bufs[i]->Data();
        send_iovecs_[i].iov_base = const_cast<std::byte*>(data.data());
        send_iovecs_[i].iov_len = data.size();
    }

    // Keep buffer refs alive until CQE
    in_flight_bufs_ = std::move(bufs);

    // Setup msghdr for sendmsg
    std::memset(&send_msg_, 0, sizeof(send_msg_));
    send_msg_.msg_iov = send_iovecs_.data();
    send_msg_.msg_iovlen = send_iovecs_.size();

    ++pending_io_;
    if (!ring_.PrepSendMsg(send_ev_, Fd(), &send_msg_, MSG_NOSIGNAL)) {
        --pending_io_;
        in_flight_bufs_.clear();
        send_iovecs_.clear();
        spdlog::error("Session[fd={}]: [DISC:SQE_FULL_SEND] PrepSendMsg failed", Fd());
        Disconnect();
        return;
    }
    ring_.Submit();
}

} // namespace servercore::io
