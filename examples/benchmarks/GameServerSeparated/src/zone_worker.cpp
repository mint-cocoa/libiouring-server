#include "zone_worker.h"

#include <cstdio>

namespace bench {

void ZoneWorker::Start(std::atomic<bool>& running) {
    thread_ = std::thread([this, &running]() { Run(running); });
}

void ZoneWorker::Stop() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ZoneWorker::Run(std::atomic<bool>& running) {
    std::printf("ZoneWorker %d started with %zu zones\n",
                worker_id_, zones_.size());

    // Each zone worker owns a BufferPool for building packets on the worker thread
    servercore::buffer::BufferPool pool;

    for (auto& zone : zones_) {
        zone->SetBufferPool(&pool);
    }

    auto last_tick = std::chrono::steady_clock::now();

    while (running.load(std::memory_order_relaxed)) {
        bool did_work = false;

        for (auto& zone : zones_) {
            auto count = zone->DrainPacketQueue();
            if (count > 0) did_work = true;
        }

        // Zone tick at ~20Hz
        auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= std::chrono::milliseconds{50}) {
            for (auto& zone : zones_) {
                zone->Tick(pool);
            }
            last_tick = now;
        }

        if (!did_work) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    std::printf("ZoneWorker %d stopped\n", worker_id_);
}

} // namespace bench
