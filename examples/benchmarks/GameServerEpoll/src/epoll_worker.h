#pragma once

#include "types.h"
#include "tcp_listener.h"
#include "epoll_connection.h"
#include "zone_manager.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace bench {

// Per-connection state tracked by the worker
struct ConnectionState {
    std::unique_ptr<EpollConnection> conn;
    PlayerId player_id = 0;
    ZoneId zone_id = 0;
    bool logged_in = false;
    bool in_game = false;
};

class EpollWorker {
public:
    EpollWorker(WorkerId id, uint16_t port, ZoneManager& zone_mgr, std::atomic<bool>& running);
    ~EpollWorker();

    EpollWorker(const EpollWorker&) = delete;
    EpollWorker& operator=(const EpollWorker&) = delete;

    // Main event loop (blocks until running_ becomes false)
    void Run();

private:
    void AddToEpoll(int fd, uint32_t events);
    void ModEpoll(int fd, uint32_t events);
    void RemoveFromEpoll(int fd);

    void HandleAccept();
    void HandleClient(int fd, uint32_t events);
    void DisconnectClient(int fd);

    WorkerId worker_id_;
    uint16_t port_;
    ZoneManager& zone_mgr_;
    std::atomic<bool>& running_;

    int epoll_fd_ = -1;
    TcpListener listener_;
    std::unordered_map<int, ConnectionState> connections_;
};

} // namespace bench
