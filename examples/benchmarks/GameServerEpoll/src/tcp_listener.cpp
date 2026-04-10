#include "tcp_listener.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace bench {

TcpListener::~TcpListener() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool TcpListener::Listen(uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        std::fprintf(stderr, "socket() failed: %s\n", std::strerror(errno));
        return false;
    }

    int opt = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "bind() failed: %s\n", std::strerror(errno));
        return false;
    }

    if (::listen(fd_, SOMAXCONN) < 0) {
        std::fprintf(stderr, "listen() failed: %s\n", std::strerror(errno));
        return false;
    }

    return true;
}

int TcpListener::Accept() {
    int client_fd = ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        return -1;  // EAGAIN or error
    }

    // Enable TCP_NODELAY
    int opt = 1;
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    return client_fd;
}

} // namespace bench
