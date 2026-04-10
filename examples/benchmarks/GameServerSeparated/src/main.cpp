#include "bench_session.h"
#include "zone_manager.h"
#include "zone_worker.h"

#include <servercore/ring/IoRing.h>
#include <servercore/io/Session.h>
#include <servercore/io/Listener.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/Types.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace servercore;
using namespace servercore::ring;
using namespace servercore::io;
using namespace servercore::buffer;

static std::atomic<bool> g_running{true};

static void SignalHandler(int) { g_running = false; }

// Shared zone manager (thread-safe)
static bench::ZoneManager g_zone_mgr;

static void RunIoThread(int io_id, uint16_t port) {
    IoRingConfig cfg{
        .queue_depth = 4096,
    };

    auto ring_result = IoRing::Create(cfg);
    if (!ring_result) {
        std::fprintf(stderr, "IO thread %d: IoRing::Create failed\n", io_id);
        return;
    }
    auto ring = std::move(*ring_result);
    IoRing::SetCurrent(ring.get());

    BufferPool pool;

    auto wid = static_cast<bench::WorkerId>(io_id);

    SessionFactory factory = [wid](int fd, IoRing& r,
                                   BufferPool& p,
                                   ContextId /*ctx*/) -> SessionRef {
        auto sess = std::make_shared<bench::BenchSession>(fd, r, p, g_zone_mgr, wid);
        sess->Start();
        return sess;
    };

    Address addr{"0.0.0.0", port};
    auto listener = std::make_shared<Listener>(
        *ring, pool, addr, std::move(factory), ContextId(io_id));

    auto start_result = listener->Start();
    if (!start_result) {
        std::fprintf(stderr, "IO thread %d: Listener::Start failed\n", io_id);
        return;
    }

    // IO thread: only Dispatch + ProcessPostedTasks (no zone ticking)
    while (g_running) {
        ring->ProcessPostedTasks();
        ring->Dispatch(std::chrono::milliseconds{5});
    }

    listener->Stop();
}

int main(int argc, char* argv[]) {
    uint16_t port         = 7777;
    int      io_threads   = 4;
    int      zone_workers = 2;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--io-threads" && i + 1 < argc) {
            io_threads = std::stoi(argv[++i]);
        } else if (arg == "--zone-workers" && i + 1 < argc) {
            zone_workers = std::stoi(argv[++i]);
        }
    }

    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::printf("GameServerSeparated starting on port %u with %d IO threads, %d zone workers\n",
                port, io_threads, zone_workers);

    // Create zone workers and assign zones
    auto all_zones = g_zone_mgr.GetAllZones();

    std::vector<std::unique_ptr<bench::ZoneWorker>> workers;
    workers.reserve(zone_workers);
    for (int i = 0; i < zone_workers; ++i) {
        workers.push_back(std::make_unique<bench::ZoneWorker>(i));
    }

    // Distribute zones across workers (round-robin)
    for (std::size_t i = 0; i < all_zones.size(); ++i) {
        workers[i % zone_workers]->AddZone(all_zones[i]);
    }

    // Start zone workers
    for (auto& w : workers) {
        w->Start(g_running);
    }

    // Start IO threads
    std::vector<std::thread> io_pool;
    io_pool.reserve(io_threads);
    for (int i = 0; i < io_threads; ++i) {
        io_pool.emplace_back(RunIoThread, i, port);
    }

    // Wait for IO threads
    for (auto& t : io_pool) {
        t.join();
    }

    // Stop zone workers
    for (auto& w : workers) {
        w->Stop();
    }

    std::printf("GameServerSeparated stopped.\n");
    return 0;
}
