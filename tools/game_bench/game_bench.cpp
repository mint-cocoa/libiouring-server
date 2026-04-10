// game_bench.cpp — io_uring-based game protocol load test client.
// Tests the full Login → EnterGame → Move/Attack flow.
// Supports real-time TUI dashboard with --live flag.
// Supports zone splitting with --rooms N.

#include "BenchClient.h"
#include "Config.h"
#include "LiveStats.h"
#include "Stats.h"

#include <liburing.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace bench;
using Clock = std::chrono::steady_clock;

// ── Worker thread ────────────────────────────────────────────────

static void WorkerThread(
    int thread_id,
    const Config& cfg,
    int global_conn_base,
    std::atomic<uint32_t>* room_ids,
    std::atomic<bool>& running,
    std::atomic<bool>& measuring,
    LiveStats& live,
    ThreadStats& result)
{
    // Resolve server address
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(cfg.port);
    inet_pton(AF_INET, cfg.host, &addr.sin_addr);

    // Init io_uring
    struct io_uring ring;
    if (io_uring_queue_init(512, &ring, 0) < 0) {
        std::fprintf(stderr, "[T%d] io_uring_queue_init failed\n", thread_id);
        return;
    }

    // Create clients
    std::vector<std::unique_ptr<BenchClient>> clients;
    clients.reserve(cfg.conns);

    for (int i = 0; i < cfg.conns; i++) {
        auto client = std::make_unique<BenchClient>(
            static_cast<uint32_t>(i), thread_id, cfg,
            global_conn_base, room_ids, &live);

        if (!client->StartConnect(&ring, addr)) {
            continue;
        }

        clients.push_back(std::move(client));

        // Ramp delay
        if (cfg.ramp_delay_ms > 0 && i + 1 < cfg.conns) {
            io_uring_submit(&ring);

            struct __kernel_timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = static_cast<long>(cfg.ramp_delay_ms) * 1'000'000L;

            struct io_uring_cqe* cqe;
            while (io_uring_wait_cqe_timeout(&ring, &cqe, &ts) == 0) {
                uint64_t tag = io_uring_cqe_get_data64(cqe);
                uint32_t ci = TagConnIdx(tag);
                OpType op    = TagOp(tag);

                if (ci < clients.size()) {
                    auto& c = clients[ci];
                    switch (op) {
                    case OpType::Connect: c->OnConnect(&ring, cqe->res); break;
                    case OpType::Recv:    c->OnRecvComplete(&ring, cqe->res); break;
                    case OpType::Send:    c->OnSendComplete(&ring, cqe->res); break;
                    }
                }
                io_uring_cqe_seen(&ring, cqe);
                ts.tv_sec = 0;
                ts.tv_nsec = 0;
            }
        }
    }

    io_uring_submit(&ring);

    // Main loop
    while (running.load(std::memory_order_relaxed)) {
        struct __kernel_timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1'000'000;

        struct io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

        bool is_measuring = measuring.load(std::memory_order_relaxed);

        if (ret == 0) {
            unsigned head;
            unsigned count = 0;
            io_uring_for_each_cqe(&ring, head, cqe) {
                uint64_t tag = io_uring_cqe_get_data64(cqe);
                uint32_t ci = TagConnIdx(tag);
                OpType op    = TagOp(tag);

                if (ci < clients.size()) {
                    auto& c = clients[ci];
                    switch (op) {
                    case OpType::Connect: c->OnConnect(&ring, cqe->res); break;
                    case OpType::Recv:    c->OnRecvComplete(&ring, cqe->res); break;
                    case OpType::Send:    c->OnSendComplete(&ring, cqe->res); break;
                    }
                }
                count++;
            }
            io_uring_cq_advance(&ring, count);
        }

        uint64_t now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now().time_since_epoch()).count());

        for (size_t i = 0; i < clients.size(); i++) {
            auto& c = clients[i];
            auto st = c->state();
            if (st != BotState::Connecting && st != BotState::LoggingIn
                && st != BotState::Done) {
                c->MaybeSendGameplay(&ring, now_ns, is_measuring);
            }
        }

        io_uring_submit(&ring);
    }

    for (auto& c : clients) {
        c->FlushStats(result);
        c->Close();
    }

    io_uring_queue_exit(&ring);
}

// ── Dashboard rendering ──────────────────────────────────────────

// ANSI escape helpers
static constexpr const char* ESC_CLEAR   = "\033[2J\033[H";
static constexpr const char* ESC_BOLD    = "\033[1m";
static constexpr const char* ESC_DIM     = "\033[2m";
static constexpr const char* ESC_GREEN   = "\033[32m";
static constexpr const char* ESC_YELLOW  = "\033[33m";
static constexpr const char* ESC_CYAN    = "\033[36m";
static constexpr const char* ESC_RED     = "\033[31m";
static constexpr const char* ESC_RESET   = "\033[0m";
static constexpr const char* ESC_BAR_FG  = "\033[42m";  // green bg
static constexpr const char* ESC_BAR_BG  = "\033[100m"; // dark gray bg

static void DrawBar(int filled, int total, int width) {
    if (total == 0) total = 1;
    int bar_filled = (filled * width) / total;
    if (bar_filled > width) bar_filled = width;

    std::printf("%s", ESC_BAR_FG);
    for (int i = 0; i < bar_filled; i++) std::putchar(' ');
    std::printf("%s", ESC_BAR_BG);
    for (int i = bar_filled; i < width; i++) std::putchar(' ');
    std::printf("%s", ESC_RESET);
}

static void FormatNum(char* buf, size_t sz, uint64_t n) {
    if (n >= 1'000'000)
        std::snprintf(buf, sz, "%lu,%03lu,%03lu",
                      n / 1'000'000, (n / 1'000) % 1'000, n % 1'000);
    else if (n >= 1'000)
        std::snprintf(buf, sz, "%lu,%03lu", n / 1'000, n % 1'000);
    else
        std::snprintf(buf, sz, "%lu", n);
}

static void RenderDashboard(
    const Config& cfg,
    const LiveStats& live,
    const LiveStats::Snapshot& prev,
    double elapsed_total,
    int phase,          // 0=warmup, 1=measuring
    double phase_elapsed,
    double phase_total)
{
    int total_conns = cfg.threads * cfg.conns;
    int conn = live.connected.load(std::memory_order_relaxed);
    int igame = live.in_game.load(std::memory_order_relaxed);
    int fail = live.failed.load(std::memory_order_relaxed);

    auto snap = live.TakeSnapshot();

    // Compute per-second rates (delta from prev snapshot)
    double dt = 1.0;  // 1 second interval
    uint64_t d_tx_m = snap.tx_move   - prev.tx_move;
    uint64_t d_tx_a = snap.tx_attack - prev.tx_attack;
    uint64_t d_rx_m = snap.rx_move   - prev.rx_move;
    uint64_t d_rx_a = snap.rx_attack - prev.rx_attack;
    uint64_t d_rx_d = snap.rx_damage - prev.rx_damage;
    uint64_t d_lat_sum   = snap.lat_sum   - prev.lat_sum;
    uint64_t d_lat_count = snap.lat_count - prev.lat_count;

    double tx_rate = static_cast<double>(d_tx_m + d_tx_a) / dt;
    double rx_rate = static_cast<double>(d_rx_m + d_rx_a + d_rx_d) / dt;
    double avg_lat_us = d_lat_count > 0
        ? static_cast<double>(d_lat_sum) / static_cast<double>(d_lat_count) / 1000.0
        : 0.0;

    // Time display
    int t_elapsed = static_cast<int>(phase_elapsed);
    int t_total   = static_cast<int>(phase_total);

    char num_buf[32];

    std::printf("%s", ESC_CLEAR);

    // ── Header
    std::printf("%s", ESC_BOLD);
    std::printf(" game_bench");
    if (cfg.label[0] != '\0') std::printf(" [%s]", cfg.label);
    std::printf("     %s:%d     %dT x %dC = %d clients",
                cfg.host, cfg.port, cfg.threads, cfg.conns, total_conns);
    if (cfg.rooms > 0)
        std::printf("  (%d rooms)", cfg.rooms);
    std::printf("%s\n", ESC_RESET);

    // Phase + timer bar
    std::printf(" %s%s%s ",
                phase == 0 ? ESC_YELLOW : ESC_GREEN,
                phase == 0 ? "WARMUP   " : "MEASURING",
                ESC_RESET);
    std::printf("[");
    int bar_w = 30;
    int bar_fill = t_total > 0 ? (t_elapsed * bar_w) / t_total : 0;
    if (bar_fill > bar_w) bar_fill = bar_w;
    for (int i = 0; i < bar_fill; i++) std::printf("=");
    if (bar_fill < bar_w) std::printf(">");
    for (int i = bar_fill + 1; i < bar_w; i++) std::printf(" ");
    std::printf("] %d/%ds", t_elapsed, t_total);
    std::printf("   %s%.1fs total%s\n\n", ESC_DIM, elapsed_total, ESC_RESET);

    // ── Connections
    std::printf(" %sCONNECTIONS%s\n", ESC_BOLD, ESC_RESET);
    std::printf("   Connected  ");
    DrawBar(conn, total_conns, 25);
    std::printf("  %s%d%s/%d", conn == total_conns ? ESC_GREEN : ESC_YELLOW,
                conn, ESC_RESET, total_conns);
    if (fail > 0) std::printf("  %s(%d failed)%s", ESC_RED, fail, ESC_RESET);
    std::printf("\n");

    std::printf("   In-game    ");
    DrawBar(igame, total_conns, 25);
    std::printf("  %s%d%s/%d\n\n", igame == total_conns ? ESC_GREEN : ESC_YELLOW,
                igame, ESC_RESET, total_conns);

    // ── Throughput
    std::printf(" %sTHROUGHPUT%s %s(instant)%s\n", ESC_BOLD, ESC_RESET, ESC_DIM, ESC_RESET);

    FormatNum(num_buf, sizeof(num_buf), d_tx_m);
    std::printf("   %sTX%s  move %s%8s%s/s", ESC_CYAN, ESC_RESET, ESC_BOLD, num_buf, ESC_RESET);
    FormatNum(num_buf, sizeof(num_buf), d_tx_a);
    std::printf("   atk %s%6s%s/s", ESC_BOLD, num_buf, ESC_RESET);
    FormatNum(num_buf, sizeof(num_buf), d_tx_m + d_tx_a);
    std::printf("   total %s%8s%s/s\n", ESC_BOLD, num_buf, ESC_RESET);

    FormatNum(num_buf, sizeof(num_buf), d_rx_m);
    std::printf("   %sRX%s  move %s%8s%s/s", ESC_GREEN, ESC_RESET, ESC_BOLD, num_buf, ESC_RESET);
    FormatNum(num_buf, sizeof(num_buf), d_rx_a);
    std::printf("   atk %s%6s%s/s", ESC_BOLD, num_buf, ESC_RESET);
    FormatNum(num_buf, sizeof(num_buf), d_rx_m + d_rx_a + d_rx_d);
    std::printf("   total %s%8s%s/s\n", ESC_BOLD, num_buf, ESC_RESET);

    double factor = tx_rate > 0 ? rx_rate / tx_rate : 0;
    std::printf("   Broadcast factor: %s%.1fx%s\n\n", ESC_BOLD, factor, ESC_RESET);

    // ── Latency
    std::printf(" %sBROADCAST LATENCY%s %s(avg this second)%s\n",
                ESC_BOLD, ESC_RESET, ESC_DIM, ESC_RESET);

    if (avg_lat_us > 0) {
        // Bar visualization: log scale, 1us = 0, 100ms = full
        int lat_bar_w = 40;
        double log_val = std::log10(avg_lat_us + 1.0);
        double log_max = 5.0;  // 100,000 us = 100ms
        int lat_fill = static_cast<int>((log_val / log_max) * lat_bar_w);
        if (lat_fill < 0) lat_fill = 0;
        if (lat_fill > lat_bar_w) lat_fill = lat_bar_w;

        const char* lat_color = avg_lat_us < 1000 ? ESC_GREEN
                              : avg_lat_us < 5000 ? ESC_YELLOW
                              : ESC_RED;

        std::printf("   avg  %s%s%8.0f us%s  ", ESC_BOLD, lat_color, avg_lat_us, ESC_RESET);
        std::printf("%s", lat_color);
        for (int i = 0; i < lat_fill; i++) std::printf("\xe2\x96\x88");
        std::printf("%s", ESC_RESET);
        std::printf("%s", ESC_DIM);
        for (int i = lat_fill; i < lat_bar_w; i++) std::printf("\xe2\x96\x91");
        std::printf("%s\n", ESC_RESET);

        uint64_t max_ns = snap.lat_max;
        std::printf("   max  %s%8lu us%s", ESC_DIM, max_ns / 1000, ESC_RESET);
        FormatNum(num_buf, sizeof(num_buf), d_lat_count);
        std::printf("     %ssamples: %s%s\n", ESC_DIM, num_buf, ESC_RESET);
    } else {
        std::printf("   %s(waiting for data...)%s\n", ESC_DIM, ESC_RESET);
    }

    // ── Cumulative
    std::printf("\n %sCUMULATIVE%s\n", ESC_BOLD, ESC_RESET);
    FormatNum(num_buf, sizeof(num_buf), snap.tx_move + snap.tx_attack);
    std::printf("   TX total: %s pkt", num_buf);
    FormatNum(num_buf, sizeof(num_buf), snap.rx_move + snap.rx_attack + snap.rx_damage);
    std::printf("     RX total: %s pkt", num_buf);
    FormatNum(num_buf, sizeof(num_buf), snap.lat_count);
    std::printf("     latency samples: %s\n", num_buf);

    std::printf("\n %s[Ctrl+C to stop]%s\n", ESC_DIM, ESC_RESET);

    std::fflush(stdout);
}

// ── Final report (same as before, for non-live + piped output) ───

static void PrintResults(const Config& cfg,
                         const std::vector<ThreadStats>& stats,
                         double elapsed_sec)
{
    int total_conns = cfg.threads * cfg.conns;

    ThreadStats agg{};
    for (auto& s : stats) {
        agg.connected += s.connected;
        agg.in_game   += s.in_game;
        agg.failed    += s.failed;
        agg.tx_move   += s.tx_move;
        agg.tx_attack += s.tx_attack;
        agg.rx_move        += s.rx_move;
        agg.rx_attack      += s.rx_attack;
        agg.rx_damage      += s.rx_damage;
        agg.rx_spawn       += s.rx_spawn;
        agg.rx_despawn     += s.rx_despawn;
        agg.rx_player_list += s.rx_player_list;
    }

    auto login_rtt = MergeAndSort(stats, &ThreadStats::login_rtt_ns);
    auto enter_rtt = MergeAndSort(stats, &ThreadStats::enter_rtt_ns);
    auto bcast_lat = MergeAndSort(stats, &ThreadStats::broadcast_latency_ns);

    std::printf("\n=== Game Benchmark");
    if (cfg.label[0] != '\0')
        std::printf(" [%s]", cfg.label);
    std::printf(" ===\n");

    std::printf("Config: %d threads x %d conns = %d total, move=%dms, attack=%dms%s",
                cfg.threads, cfg.conns, total_conns,
                cfg.move_interval_ms, cfg.attack_interval_ms,
                cfg.no_attack ? " (no-attack)" : "");
    if (cfg.rooms > 0)
        std::printf(", rooms=%d (~%d/room)", cfg.rooms,
                    total_conns / cfg.rooms);
    std::printf("\n");
    std::printf("Duration: %.1fs (warmup: %ds)\n", elapsed_sec, cfg.warmup);
    std::printf("Target: %s:%d\n", cfg.host, cfg.port);

    std::printf("\n--- Connection ---\n");
    std::printf("Connected: %d/%d  In-game: %d/%d  Failed: %d\n",
                agg.connected, total_conns, agg.in_game, total_conns, agg.failed);

    std::printf("\n--- Handshake RTT (us) ---\n");
    PrintLatencyLine("Login:", login_rtt);
    PrintLatencyLine("Enter:", enter_rtt);

    std::printf("\n--- Broadcast Latency (us) ---\n");
    PrintLatencyLine("S_MOVE:", bcast_lat);

    std::printf("\n--- Throughput ---\n");
    if (elapsed_sec > 0) {
        double tx_move_s   = static_cast<double>(agg.tx_move) / elapsed_sec;
        double tx_attack_s = static_cast<double>(agg.tx_attack) / elapsed_sec;
        double rx_move_s   = static_cast<double>(agg.rx_move) / elapsed_sec;
        double rx_attack_s = static_cast<double>(agg.rx_attack) / elapsed_sec;
        double rx_damage_s = static_cast<double>(agg.rx_damage) / elapsed_sec;
        double tx_total    = tx_move_s + tx_attack_s;
        double rx_total    = rx_move_s + rx_attack_s + rx_damage_s;

        std::printf("  TX: move=%.0f/s  attack=%.0f/s  total=%.0f/s\n",
                    tx_move_s, tx_attack_s, tx_total);
        std::printf("  RX: move=%.0f/s  attack=%.0f/s  damage=%.0f/s  total=%.0f/s\n",
                    rx_move_s, rx_attack_s, rx_damage_s, rx_total);

        if (tx_total > 0)
            std::printf("  Broadcast factor: %.1fx\n", rx_total / tx_total);
    } else {
        std::printf("  (no measurement data)\n");
    }

    std::printf("\n");
}

// ── CLI parsing ──────────────────────────────────────────────────

static Config ParseArgs(int argc, char* argv[], bool& live_mode) {
    Config cfg;
    live_mode = isatty(STDOUT_FILENO);  // auto-detect: tty = live

    for (int i = 1; i < argc; ++i) {
        auto match = [&](const char* name) {
            return std::strcmp(argv[i], name) == 0 && i + 1 < argc;
        };

        if (match("--host"))              cfg.host = argv[++i];
        else if (match("--port"))         cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (match("--threads"))      cfg.threads = std::atoi(argv[++i]);
        else if (match("--conns"))        cfg.conns = std::atoi(argv[++i]);
        else if (match("--duration"))     cfg.duration = std::atoi(argv[++i]);
        else if (match("--warmup"))       cfg.warmup = std::atoi(argv[++i]);
        else if (match("--move-interval")) cfg.move_interval_ms = std::atoi(argv[++i]);
        else if (match("--attack-interval")) cfg.attack_interval_ms = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--no-attack") == 0) cfg.no_attack = true;
        else if (match("--ramp-delay"))   cfg.ramp_delay_ms = std::atoi(argv[++i]);
        else if (match("--rooms"))        cfg.rooms = std::atoi(argv[++i]);
        else if (match("--label"))        cfg.label = argv[++i];
        else if (match("--csv"))          cfg.csv_file = argv[++i];
        else if (std::strcmp(argv[i], "--live") == 0)   live_mode = true;
        else if (std::strcmp(argv[i], "--no-live") == 0) live_mode = false;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "Usage: game_bench [options]\n"
                "  --host HOST              Server address (default: 127.0.0.1)\n"
                "  --port PORT              Server port (default: 7777)\n"
                "  --threads N              Worker threads (default: 4)\n"
                "  --conns N                Connections per thread (default: 10)\n"
                "  --duration SECS          Measurement time (default: 30)\n"
                "  --warmup SECS            Warmup time (default: 5)\n"
                "  --move-interval MS       C_MOVE send period (default: 50)\n"
                "  --attack-interval MS     C_ATTACK send period (default: 500)\n"
                "  --no-attack              Disable C_ATTACK\n"
                "  --ramp-delay MS          Delay between connections (default: 10)\n"
                "  --rooms N                Split into N rooms (default: 0 = single zone)\n"
                "  --label TEXT             Result label\n"
                "  --csv FILE               Per-second CSV output\n"
                "  --live                   Force live dashboard (default: auto-detect tty)\n"
                "  --no-live                Disable live dashboard\n");
            std::exit(0);
        }
    }

    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.conns < 1) cfg.conns = 1;
    if (cfg.duration < 1) cfg.duration = 1;
    if (cfg.warmup < 0) cfg.warmup = 0;
    if (cfg.move_interval_ms < 1) cfg.move_interval_ms = 1;
    if (cfg.attack_interval_ms < 1) cfg.attack_interval_ms = 1;
    if (cfg.rooms < 0) cfg.rooms = 0;

    return cfg;
}

// ── Main ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    bool live_mode = false;
    Config cfg = ParseArgs(argc, argv, live_mode);

    int total_conns = cfg.threads * cfg.conns;

    // Shared room_ids for --rooms mode
    std::unique_ptr<std::atomic<uint32_t>[]> room_ids;
    if (cfg.rooms > 0) {
        room_ids = std::make_unique<std::atomic<uint32_t>[]>(cfg.rooms);
        for (int i = 0; i < cfg.rooms; i++)
            room_ids[i].store(0, std::memory_order_relaxed);
    }

    if (!live_mode) {
        std::printf("game_bench: %s:%d, %d threads x %d conns = %d, "
                    "move=%dms, attack=%dms, duration=%ds, warmup=%ds",
                    cfg.host, cfg.port, cfg.threads, cfg.conns, total_conns,
                    cfg.move_interval_ms, cfg.attack_interval_ms,
                    cfg.duration, cfg.warmup);
        if (cfg.rooms > 0)
            std::printf(", rooms=%d", cfg.rooms);
        std::printf("\n");
    }

    std::atomic<bool> running{true};
    std::atomic<bool> measuring{false};

    LiveStats live;
    std::vector<ThreadStats> stats(cfg.threads);
    std::vector<std::thread> workers;
    workers.reserve(cfg.threads);

    auto bench_start = Clock::now();

    for (int i = 0; i < cfg.threads; i++) {
        int global_conn_base = i * cfg.conns;
        workers.emplace_back(WorkerThread, i,
                             std::cref(cfg),
                             global_conn_base,
                             room_ids.get(),
                             std::ref(running),
                             std::ref(measuring),
                             std::ref(live),
                             std::ref(stats[i]));
    }

    // ── Warmup phase ──
    if (live_mode) {
        auto prev_snap = live.TakeSnapshot();
        for (int s = 0; s < cfg.warmup; s++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            double total_elapsed = std::chrono::duration<double>(Clock::now() - bench_start).count();
            RenderDashboard(cfg, live, prev_snap, total_elapsed,
                            0, static_cast<double>(s + 1), static_cast<double>(cfg.warmup));
            prev_snap = live.TakeSnapshot();
        }
    } else {
        if (cfg.warmup > 0) {
            std::printf("Warming up for %ds...\n", cfg.warmup);
            std::this_thread::sleep_for(std::chrono::seconds(cfg.warmup));
        }
    }

    // ── Measurement phase ──
    measuring.store(true, std::memory_order_release);
    auto measure_start = Clock::now();

    if (live_mode) {
        auto prev_snap = live.TakeSnapshot();
        for (int s = 0; s < cfg.duration; s++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            double total_elapsed = std::chrono::duration<double>(Clock::now() - bench_start).count();
            RenderDashboard(cfg, live, prev_snap, total_elapsed,
                            1, static_cast<double>(s + 1), static_cast<double>(cfg.duration));
            prev_snap = live.TakeSnapshot();
        }
    } else {
        std::printf("Measuring for %ds...\n", cfg.duration);
        std::this_thread::sleep_for(std::chrono::seconds(cfg.duration));
    }

    auto measure_end = Clock::now();

    // Stop
    running.store(false, std::memory_order_release);
    for (auto& t : workers)
        t.join();

    // Clear dashboard before printing final results
    if (live_mode)
        std::printf("%s", ESC_CLEAR);

    double elapsed = std::chrono::duration<double>(measure_end - measure_start).count();
    PrintResults(cfg, stats, elapsed);

    return 0;
}
