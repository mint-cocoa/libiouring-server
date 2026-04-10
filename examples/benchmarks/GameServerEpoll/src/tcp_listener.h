#pragma once

#include <cstdint>

namespace bench {

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // Create, bind, listen with SO_REUSEPORT + SO_REUSEADDR.
    // Returns true on success.
    bool Listen(uint16_t port);

    int Fd() const { return fd_; }

    // Accept a new connection. Returns fd or -1 on EAGAIN/error.
    int Accept();

private:
    int fd_ = -1;
};

} // namespace bench
