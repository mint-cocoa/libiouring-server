#pragma once

#include <servercore/buffer/RecvBuffer.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/buffer/SendQueue.h>
#include <servercore/io/SocketHandle.h>
#include <servercore/Types.h>
#include <servercore/Error.h>
#include <servercore/ring/EventHandler.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include <sys/socket.h>
#include <sys/uio.h>

namespace servercore::ring { class IoRing; }

namespace servercore::io {

using buffer::BufferPool;
using buffer::SendBufferRef;
using ring::IoRing;

class Session;
using SessionRef = std::shared_ptr<Session>;

class Session : public ring::EventHandler {
public:
    using ConnectedCallback = std::move_only_function<void(SessionRef)>;
    using DisconnectCallback = std::move_only_function<void(SessionRef)>;
    using SendOverflowCallback = std::move_only_function<void(SessionRef)>;

    Session(int fd, IoRing& ring, BufferPool& pool);
    ~Session() override;

    void Start();
    std::expected<void, io::IoError> Send(buffer::SendBufferRef buf);
    void Disconnect();
    void ReleaseOwnership();

    bool Disconnecting() const { return disconnecting_; }

    int Fd() const { return socket_.Get(); }
    servercore::SessionId GetSessionId() const { return session_id_; }
    void SetSessionId(servercore::SessionId id) { session_id_ = id; }
    void SetConnectedCallback(ConnectedCallback cb) { on_connected_ = std::move(cb); }
    void SetDisconnectCallback(DisconnectCallback cb) { on_disconnect_ = std::move(cb); }
    void SetSendOverflowCallback(SendOverflowCallback cb) { on_send_overflow_ = std::move(cb); }

protected:
    // Subclass implements packet-level processing.
    // Fast path: called with provided buffer data (zero-copy, in-place).
    // Slow path: called with RecvBuffer data after reassembly.
    virtual void OnRecv(std::span<const std::byte> data) = 0;
    virtual void OnConnected() {}
    virtual void OnDisconnected() {}

    BufferPool& Pool() { return pool_; }
    IoRing& Ring() { return ring_; }

private:
    void RegisterRecv();
    void RegisterSend();
    void SendBatch(std::vector<SendBufferRef> bufs);
    void OnRecv(ring::RecvEvent& ev, std::int32_t result, std::uint32_t flags) override;
    void OnSend(ring::SendEvent& ev, std::int32_t result) override;
    void OnDisconnect(ring::DisconnectEvent& ev, std::int32_t result) override;

    // Release self-ownership when all in-flight I/O has settled.
    void TryRelease();

    SocketHandle socket_;
    IoRing& ring_;
    BufferPool& pool_;
    buffer::SendQueue send_queue_;
    buffer::RecvBuffer recv_buf_;
    servercore::SessionId session_id_ = 0;
    ConnectedCallback on_connected_;
    DisconnectCallback on_disconnect_;
    SendOverflowCallback on_send_overflow_;
    bool disconnecting_ = false;

    // Counts in-flight io_uring ops whose CQEs reference Session members.
    // self_ref_ must not be released until this reaches zero.
    // Accessed from I/O thread only — no synchronization needed.
    int pending_io_ = 0;

    // Self-ownership: keeps Session alive during active I/O.
    // Released only via TryRelease() after all in-flight ops complete.
    std::shared_ptr<Session> self_ref_;

    // io_uring events — lifetime tied to Session
    ring::RecvEvent recv_ev_;
    ring::SendEvent send_ev_;
    ring::DisconnectEvent disconnect_ev_;

    // sendmsg state — must stay valid until CQE
    struct msghdr send_msg_{};
    std::vector<struct iovec> send_iovecs_;
    std::vector<SendBufferRef> in_flight_bufs_;
};

} // namespace servercore::io
