#include "epoll_connection.h"

#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>

namespace bench {

EpollConnection::EpollConnection(int fd)
    : fd_(fd)
    , recv_buf_(kRecvBufSize)
{
}

EpollConnection::~EpollConnection() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool EpollConnection::OnReadable(std::function<void(uint16_t, const std::byte*, uint32_t)> on_packet) {
    // Edge-triggered: must drain all available data
    for (;;) {
        if (recv_len_ >= recv_buf_.size()) {
            // Buffer full — grow if under max, otherwise protocol error
            if (recv_buf_.size() >= kMaxPacket * 2) {
                return false;
            }
            recv_buf_.resize(recv_buf_.size() * 2);
        }

        ssize_t n = ::recv(fd_,
                           recv_buf_.data() + recv_len_,
                           recv_buf_.size() - recv_len_,
                           0);
        if (n > 0) {
            recv_len_ += static_cast<size_t>(n);
        } else if (n == 0) {
            // Peer closed
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No more data available
            }
            if (errno == EINTR) {
                continue;
            }
            return false;  // Error
        }
    }

    // Parse complete packets
    size_t consumed = 0;
    while (recv_len_ - consumed >= kHeaderSize) {
        auto* ptr = recv_buf_.data() + consumed;

        uint16_t pkt_size;
        std::memcpy(&pkt_size, ptr, 2);

        if (pkt_size < kHeaderSize || pkt_size > kMaxPacket) {
            return false;  // Invalid packet
        }
        if (recv_len_ - consumed < pkt_size) {
            break;  // Incomplete packet
        }

        uint16_t msg_id;
        std::memcpy(&msg_id, ptr + 2, 2);

        on_packet(msg_id, ptr + kHeaderSize, pkt_size - kHeaderSize);
        consumed += pkt_size;
    }

    // Compact buffer
    if (consumed > 0) {
        if (consumed < recv_len_) {
            std::memmove(recv_buf_.data(), recv_buf_.data() + consumed, recv_len_ - consumed);
        }
        recv_len_ -= consumed;
    }

    return true;
}

bool EpollConnection::OnWritable() {
    std::lock_guard lock(send_mutex_);
    while (!send_queue_.empty()) {
        auto& front = send_queue_.front();
        size_t remaining = front.size() - send_offset_;

        ssize_t n = ::send(fd_, front.data() + send_offset_, remaining, MSG_NOSIGNAL);
        if (n > 0) {
            send_offset_ += static_cast<size_t>(n);
            if (send_offset_ >= front.size()) {
                send_queue_.pop_front();
                send_offset_ = 0;
            }
        } else if (n == 0) {
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // Would block, try again later
            }
            if (errno == EINTR) {
                continue;
            }
            return false;  // Error
        }
    }
    return true;
}

void EpollConnection::QueueSend(const uint8_t* data, size_t len) {
    std::lock_guard lock(send_mutex_);
    send_queue_.emplace_back(data, data + len);
}

void EpollConnection::QueueSend(std::vector<uint8_t> pkt) {
    std::lock_guard lock(send_mutex_);
    send_queue_.push_back(std::move(pkt));
}

} // namespace bench
