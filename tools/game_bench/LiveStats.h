#pragma once

// LiveStats.h — Lock-free atomic counters for real-time dashboard.
// Updated by worker threads, read by main thread every second.

#include <atomic>
#include <cstdint>

namespace bench {

struct LiveStats {
    // Connection state
    std::atomic<int> connected{0};
    std::atomic<int> in_game{0};
    std::atomic<int> failed{0};

    // Throughput (cumulative, compute delta for /s)
    std::atomic<uint64_t> tx_move{0};
    std::atomic<uint64_t> tx_attack{0};
    std::atomic<uint64_t> rx_move{0};
    std::atomic<uint64_t> rx_attack{0};
    std::atomic<uint64_t> rx_damage{0};

    // Broadcast latency: maintain running sum + count for approximate p50
    // (accurate percentiles computed at end from per-client vectors)
    std::atomic<uint64_t> latency_sum_ns{0};
    std::atomic<uint64_t> latency_count{0};
    std::atomic<uint64_t> latency_max_ns{0};

    // Snapshot for delta computation
    struct Snapshot {
        uint64_t tx_move, tx_attack;
        uint64_t rx_move, rx_attack, rx_damage;
        uint64_t lat_sum, lat_count, lat_max;
    };

    Snapshot TakeSnapshot() const {
        return {
            tx_move.load(std::memory_order_relaxed),
            tx_attack.load(std::memory_order_relaxed),
            rx_move.load(std::memory_order_relaxed),
            rx_attack.load(std::memory_order_relaxed),
            rx_damage.load(std::memory_order_relaxed),
            latency_sum_ns.load(std::memory_order_relaxed),
            latency_count.load(std::memory_order_relaxed),
            latency_max_ns.load(std::memory_order_relaxed),
        };
    }

    // Atomically update max
    void UpdateLatencyMax(uint64_t val) {
        uint64_t cur = latency_max_ns.load(std::memory_order_relaxed);
        while (val > cur && !latency_max_ns.compare_exchange_weak(
                   cur, val, std::memory_order_relaxed)) {}
    }
};

} // namespace bench
