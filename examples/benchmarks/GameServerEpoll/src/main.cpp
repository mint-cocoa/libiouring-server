#include "epoll_worker.h"
#include "zone_manager.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

static void SignalHandler(int) { g_running = false; }

static bench::ZoneManager g_zone_mgr;

int main(int argc, char* argv[]) {
    uint16_t port    = 7777;
    int      threads = 4;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = std::stoi(argv[++i]);
        }
    }

    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::printf("GameServerEpoll starting on port %u with %d threads\n", port, threads);

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([i, port]() {
            bench::EpollWorker worker(
                static_cast<bench::WorkerId>(i), port, g_zone_mgr, g_running);
            worker.Run();
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    std::printf("GameServerEpoll stopped.\n");
    return 0;
}
