#include "epoll_worker.h"
#include "packet_handler.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

namespace bench {

static constexpr int kMaxEvents = 256;

EpollWorker::EpollWorker(WorkerId id, uint16_t port, ZoneManager& zone_mgr, std::atomic<bool>& running)
    : worker_id_(id)
    , port_(port)
    , zone_mgr_(zone_mgr)
    , running_(running)
{
}

EpollWorker::~EpollWorker() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

void EpollWorker::Run() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::fprintf(stderr, "Worker %d: epoll_create1 failed: %s\n",
                     worker_id_, std::strerror(errno));
        return;
    }

    if (!listener_.Listen(port_)) {
        std::fprintf(stderr, "Worker %d: listen failed\n", worker_id_);
        return;
    }

    // Register listener fd with EPOLLIN | EPOLLET
    AddToEpoll(listener_.Fd(), EPOLLIN | EPOLLET);

    epoll_event events[kMaxEvents];
    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, 1 /*ms*/);

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listener_.Fd()) {
                HandleAccept();
            } else {
                HandleClient(fd, ev);
            }
        }

        // Zone tick at ~20Hz
        auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= std::chrono::milliseconds{50}) {
            zone_mgr_.ForEachZone([](Zone& zone) {
                zone.Tick();
            });
            last_tick = now;
        }

        // Update EPOLLOUT for connections with pending sends
        for (auto& [fd, cs] : connections_) {
            if (cs.conn->HasPendingSend()) {
                ModEpoll(fd, EPOLLIN | EPOLLOUT | EPOLLET);
            }
        }
    }

    // Cleanup: close all connections
    connections_.clear();
}

void EpollWorker::AddToEpoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
}

void EpollWorker::ModEpoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void EpollWorker::RemoveFromEpoll(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void EpollWorker::HandleAccept() {
    // Edge-triggered: must drain all pending accepts
    for (;;) {
        int client_fd = listener_.Accept();
        if (client_fd < 0) break;

        auto conn = std::make_unique<EpollConnection>(client_fd);
        ConnectionState cs;
        cs.conn = std::move(conn);
        connections_[client_fd] = std::move(cs);

        AddToEpoll(client_fd, EPOLLIN | EPOLLET);
    }
}

void EpollWorker::HandleClient(int fd, uint32_t events) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& cs = it->second;

    if (events & (EPOLLERR | EPOLLHUP)) {
        DisconnectClient(fd);
        return;
    }

    if (events & EPOLLIN) {
        bool ok = cs.conn->OnReadable(
            [&](uint16_t msg_id, const std::byte* body, uint32_t body_len) {
                HandlePacket(msg_id, body, body_len,
                             *cs.conn, zone_mgr_,
                             cs.player_id, cs.zone_id, worker_id_,
                             cs.logged_in, cs.in_game);
            });
        if (!ok) {
            DisconnectClient(fd);
            return;
        }
    }

    if (events & EPOLLOUT) {
        bool ok = cs.conn->OnWritable();
        if (!ok) {
            DisconnectClient(fd);
            return;
        }
        // If send queue drained, remove EPOLLOUT
        if (!cs.conn->HasPendingSend()) {
            ModEpoll(fd, EPOLLIN | EPOLLET);
        }
    }

    // If we queued sends during packet handling, enable EPOLLOUT
    if (cs.conn->HasPendingSend()) {
        ModEpoll(fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

void EpollWorker::DisconnectClient(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& cs = it->second;

    // Remove from zone if in game
    if (cs.in_game) {
        auto* zone = zone_mgr_.FindZone(cs.zone_id);
        if (zone) {
            zone->RemovePlayer(cs.player_id);
        }
    }

    RemoveFromEpoll(fd);
    connections_.erase(it);  // EpollConnection destructor closes fd
}

} // namespace bench
