#pragma once

#include <servercore/ring/EventHandler.h>
#include <servercore/ring/IoRing.h>
#include <servercore/ring/RingEvent.h>
#include <servercore/buffer/SendBuffer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <sys/socket.h>

namespace serverweb {

using ProxyCallback = std::function<void(
    int status_code,
    std::vector<std::pair<std::string, std::string>> headers,
    std::vector<std::byte> body
)>;

using ProxyErrorCallback = std::function<void(std::string error)>;

class UpstreamSession : public servercore::ring::EventHandler {
public:
    UpstreamSession(servercore::ring::IoRing& ring,
                    servercore::buffer::BufferPool& pool);
    ~UpstreamSession() override;

    // Must be called after make_shared to set event owners
    void Init();

    void Connect(const std::string& host, std::uint16_t port,
                 std::string request_bytes,
                 ProxyCallback on_response,
                 ProxyErrorCallback on_error);

    void Close();
    bool IsConnected() const { return connected_; }
    int Fd() const { return fd_; }

protected:
    void OnConnect(servercore::ring::ConnectEvent& ev, std::int32_t result) override;
    void OnSend(servercore::ring::SendEvent& ev, std::int32_t result) override;
    void OnRecv(servercore::ring::RecvEvent& ev,
                std::int32_t result, std::uint32_t flags) override;

private:
    void StartRecv();
    void ParseResponse();
    void SendRequest();

    servercore::ring::IoRing& ring_;
    servercore::buffer::BufferPool& pool_;
    int fd_ = -1;
    bool connected_ = false;

    std::string pending_request_;
    std::vector<std::byte> recv_buffer_;

    ProxyCallback on_response_;
    ProxyErrorCallback on_error_;

    servercore::ring::ConnectEvent connect_ev_;
    servercore::ring::SendEvent send_ev_;
    servercore::ring::RecvEvent recv_ev_;

    // Send state: must stay alive until CQE
    servercore::buffer::SendBufferRef send_buf_;
    struct msghdr send_msg_{};
    struct iovec send_iov_{};

    // Keep-alive ref during async operations
    std::shared_ptr<UpstreamSession> self_ref_;

    // Connect address storage (must live until CQE)
    struct sockaddr_storage connect_addr_{};
    socklen_t connect_addr_len_ = 0;
};

} // namespace serverweb
