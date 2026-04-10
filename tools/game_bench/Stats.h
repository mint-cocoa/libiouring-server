#pragma once

// Stats.h — Per-thread statistics collection and aggregation for game_bench.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace bench {

struct ThreadStats {
    // Handshake RTT (direct response)
    std::vector<uint64_t> login_rtt_ns;
    std::vector<uint64_t> enter_rtt_ns;

    // Broadcast propagation latency (position timestamp encoding)
    std::vector<uint64_t> broadcast_latency_ns;

    // Throughput counters
    uint64_t tx_move   = 0;
    uint64_t tx_attack = 0;

    uint64_t rx_move      = 0;
    uint64_t rx_attack    = 0;
    uint64_t rx_damage    = 0;
    uint64_t rx_spawn     = 0;
    uint64_t rx_despawn   = 0;
    uint64_t rx_player_list = 0;

    // Connection state
    int connected = 0;
    int in_game   = 0;
    int failed    = 0;
};

// Compute percentile from a sorted vector. p in [0.0, 1.0].
inline uint64_t Percentile(const std::vector<uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

// Merge and sort multiple ThreadStats' RTT vectors into one sorted vector.
inline std::vector<uint64_t> MergeAndSort(
    const std::vector<ThreadStats>& all,
    std::vector<uint64_t> ThreadStats::*field)
{
    size_t total = 0;
    for (auto& s : all) total += (s.*field).size();

    std::vector<uint64_t> merged;
    merged.reserve(total);
    for (auto& s : all)
        merged.insert(merged.end(), (s.*field).begin(), (s.*field).end());

    std::sort(merged.begin(), merged.end());
    return merged;
}

inline void PrintLatencyLine(const char* label, const std::vector<uint64_t>& sorted) {
    if (sorted.empty()) {
        std::printf("  %-8s (no samples)\n", label);
        return;
    }
    std::printf("  %-8s min=%-6lu  p50=%-6lu  p95=%-6lu  p99=%-6lu  max=%-6lu  samples=%zu\n",
                label,
                sorted.front() / 1000,
                Percentile(sorted, 0.50) / 1000,
                Percentile(sorted, 0.95) / 1000,
                Percentile(sorted, 0.99) / 1000,
                sorted.back() / 1000,
                sorted.size());
}

} // namespace bench
