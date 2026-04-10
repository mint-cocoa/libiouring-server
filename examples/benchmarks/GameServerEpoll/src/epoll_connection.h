#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace bench {

class EpollConnection {
public:
    explicit EpollConnection(int fd);
    ~EpollConnection();

    EpollConnection(const EpollConnection&) = delete;
    EpollConnection& operator=(const EpollConnection&) = delete;

    int Fd() const { return fd_; }

    // Called when EPOLLIN fires. Returns false on connection error (should disconnect).
    // Calls on_packet(msg_id, body, body_len) for each complete packet.
    bool OnReadable(std::function<void(uint16_t, const std::byte*, uint32_t)> on_packet);

    // Called when EPOLLOUT fires. Returns false on connection error.
    bool OnWritable();

    // Queue data for sending
    void QueueSend(const uint8_t* data, size_t len);
    void QueueSend(std::vector<uint8_t> pkt);

    bool HasPendingSend() {
        std::lock_guard lock(send_mutex_);
        return !send_queue_.empty();
    }

private:
    static constexpr uint32_t kHeaderSize = 4;
    static constexpr uint32_t kMaxPacket = 65536;
    static constexpr size_t kRecvBufSize = 65536;

    int fd_;
    std::vector<std::byte> recv_buf_;
    size_t recv_len_ = 0;  // valid bytes in recv_buf_

    std::mutex send_mutex_;
    std::deque<std::vector<uint8_t>> send_queue_;
    size_t send_offset_ = 0;  // offset into front of send_queue_
};

} // namespace bench
