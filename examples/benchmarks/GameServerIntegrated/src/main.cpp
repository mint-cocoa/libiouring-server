#include "bench_session.h"
#include "zone_manager.h"

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

static void RunWorker(int worker_id, uint16_t port) {
    IoRingConfig cfg{
        .queue_depth = 4096,
    };

    auto ring_result = IoRing::Create(cfg);
    if (!ring_result) {
        std::fprintf(stderr, "Worker %d: IoRing::Create failed\n", worker_id);
        return;
    }
    auto ring = std::move(*ring_result);
    IoRing::SetCurrent(ring.get());

    BufferPool pool;

    auto wid = static_cast<bench::WorkerId>(worker_id);

    SessionFactory factory = [wid](int fd, IoRing& r,
                                   BufferPool& p,
                                   ContextId /*ctx*/) -> SessionRef {
        auto sess = std::make_shared<bench::BenchSession>(fd, r, p, g_zone_mgr, wid);
        sess->Start();
        return sess;
    };

    Address addr{"0.0.0.0", port};
    auto listener = std::make_shared<Listener>(
        *ring, pool, addr, std::move(factory), ContextId(worker_id));

    auto start_result = listener->Start();
    if (!start_result) {
        std::fprintf(stderr, "Worker %d: Listener::Start failed\n", worker_id);
        return;
    }

    auto last_tick = std::chrono::steady_clock::now();

    while (g_running) {
        ring->ProcessPostedTasks();
        ring->Dispatch(std::chrono::milliseconds{5});

        // Zone tick at ~20Hz
        auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= std::chrono::milliseconds{50}) {
            // Only tick zones that have players on this worker
            // For simplicity, each worker ticks all zones (idempotent ops)
            g_zone_mgr.ForEachZone([&pool](bench::Zone& zone) {
                zone.Tick(pool);
            });
            last_tick = now;
        }
    }

    listener->Stop();
}

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

    std::printf("GameServerIntegrated starting on port %u with %d threads\n", port, threads);

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(RunWorker, i, port);
    }

    for (auto& w : workers) {
        w.join();
    }

    std::printf("GameServerIntegrated stopped.\n");
    return 0;
}
