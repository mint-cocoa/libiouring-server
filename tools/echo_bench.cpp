// echo_bench.cpp — C++ io_uring-free benchmark client using epoll + non-blocking TCP
// Standalone: no ServerCore dependency, pure POSIX + epoll.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;

// ── Packet format ────────────────────────────────────────────────

// PacketHeader: [uint16 size][uint16 id]  (4 bytes, matches ServerCore)
// Echo body:    [uint32 seq_no][uint64 ts_ns][padding...]

static constexpr uint16_t ECHO_PKT_ID = 1;
static constexpr size_t   HDR_SIZE    = 4;
static constexpr size_t   BODY_MIN    = sizeof(uint32_t) + sizeof(uint64_t); // 12 bytes

// ── CLI config ───────────────────────────────────────────────────

struct Config {
    const char* host     = "127.0.0.1";
    uint16_t    port     = 9100;
    int         threads  = 4;
    int         conns    = 1;      // per thread
    int         payload  = 64;     // body size (>= 12)
    int         duration = 10;     // seconds
    int         warmup   = 2;      // seconds
    int         pipeline = 1;      // max in-flight per conn
    const char* label    = "";
};

// ── Connection state ─────────────────────────────────────────────

struct BenchConn {
    int      fd = -1;
    uint32_t next_seq  = 0;
    uint32_t in_flight = 0;

    // Send buffer (one packet at a time)
    std::vector<uint8_t> send_buf;
    size_t send_pos = 0;

    // Receive buffer (framing parser)
    std::vector<uint8_t> recv_buf;
    size_t recv_len = 0;

    bool connected = true;
};

// ── Helpers ──────────────────────────────────────────────────────

static uint64_t NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count());
}

static int MakeNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int ConnectTcp(const char* host, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    MakeNonBlocking(fd);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    int rv = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rv < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    return fd;
}

// ── Packet build / parse ─────────────────────────────────────────

static void BuildPacket(BenchConn& conn, int payload_size) {
    uint16_t pkt_size = static_cast<uint16_t>(HDR_SIZE + payload_size);

    conn.send_buf.resize(pkt_size, 0);
    conn.send_pos = 0;

    // Header
    std::memcpy(&conn.send_buf[0], &pkt_size, 2);
    uint16_t id = ECHO_PKT_ID;
    std::memcpy(&conn.send_buf[2], &id, 2);

    // Body: seq_no + ts_ns
    uint32_t seq = conn.next_seq++;
    std::memcpy(&conn.send_buf[HDR_SIZE], &seq, 4);

    uint64_t ts = NowNs();
    std::memcpy(&conn.send_buf[HDR_SIZE + 4], &ts, 8);
}

// ── Worker thread ────────────────────────────────────────────────

struct WorkerResult {
    std::vector<uint64_t> rtt_ns;    // all RTT samples (after warmup)
    uint64_t total_echoes = 0;       // total echoes including warmup
    int alive_conns = 0;
};

static void WorkerThread(
    const Config& cfg,
    std::atomic<bool>& running,
    std::atomic<bool>& measuring,
    WorkerResult& result)
{
    const int total_conns = cfg.conns;
    const int payload     = cfg.payload;
    const int pipeline    = cfg.pipeline;

    // Pre-allocate RTT vector
    result.rtt_ns.reserve(static_cast<size_t>(cfg.duration) * 200000);

    // Create connections
    std::vector<BenchConn> conns(total_conns);
    for (auto& c : conns) {
        c.fd = ConnectTcp(cfg.host, cfg.port);
        if (c.fd < 0) {
            c.connected = false;
            continue;
        }
        c.recv_buf.resize(static_cast<size_t>(HDR_SIZE + payload) * (pipeline + 2));
        c.recv_len = 0;
    }

    // Create epoll
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        std::fprintf(stderr, "epoll_create1 failed: %s\n", strerror(errno));
        return;
    }

    for (auto& c : conns) {
        if (!c.connected) continue;
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.ptr = &c;
        epoll_ctl(epfd, EPOLL_CTL_ADD, c.fd, &ev);
    }

    static constexpr int MAX_EVENTS = 256;
    epoll_event events[MAX_EVENTS];

    // Send helper: fill pipeline for a connection (called after recv and on EPOLLOUT)
    auto TrySend = [&](BenchConn* conn) {
        while (conn->connected &&
               static_cast<int>(conn->in_flight) < pipeline) {
            // If no pending send, build a new packet
            if (conn->send_pos >= conn->send_buf.size()) {
                BuildPacket(*conn, payload);
            }

            ssize_t nw = send(conn->fd,
                              conn->send_buf.data() + conn->send_pos,
                              conn->send_buf.size() - conn->send_pos, MSG_NOSIGNAL);
            if (nw > 0) {
                conn->send_pos += static_cast<size_t>(nw);
                if (conn->send_pos >= conn->send_buf.size()) {
                    conn->in_flight++;
                    conn->send_pos = conn->send_buf.size(); // mark consumed
                }
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    conn->connected = false;
                break;
            }
        }
    };

    // Main epoll loop
    while (running.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 1);

        bool is_measuring = measuring.load(std::memory_order_relaxed);

        for (int i = 0; i < n; ++i) {
            auto* conn = static_cast<BenchConn*>(events[i].data.ptr);
            if (!conn->connected) continue;

            // ── Receive ──
            if (events[i].events & EPOLLIN) {
                for (;;) {
                    ssize_t nr = recv(conn->fd,
                                      conn->recv_buf.data() + conn->recv_len,
                                      conn->recv_buf.size() - conn->recv_len, 0);
                    if (nr > 0) {
                        conn->recv_len += static_cast<size_t>(nr);
                    } else if (nr == 0) {
                        conn->connected = false;
                        break;
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            conn->connected = false;
                        break;
                    }
                }

                // Parse complete packets
                while (conn->recv_len >= HDR_SIZE) {
                    uint16_t pkt_size;
                    std::memcpy(&pkt_size, conn->recv_buf.data(), 2);

                    if (pkt_size < HDR_SIZE || pkt_size > conn->recv_buf.size()) {
                        conn->connected = false;
                        break;
                    }
                    if (conn->recv_len < pkt_size) break;

                    // Extract RTT
                    if (pkt_size >= HDR_SIZE + BODY_MIN) {
                        uint64_t send_ts;
                        std::memcpy(&send_ts,
                                    conn->recv_buf.data() + HDR_SIZE + 4, 8);
                        uint64_t rtt = NowNs() - send_ts;
                        result.total_echoes++;

                        if (is_measuring)
                            result.rtt_ns.push_back(rtt);
                    }

                    if (conn->in_flight > 0)
                        --conn->in_flight;

                    // Compact buffer
                    conn->recv_len -= pkt_size;
                    if (conn->recv_len > 0) {
                        std::memmove(conn->recv_buf.data(),
                                     conn->recv_buf.data() + pkt_size,
                                     conn->recv_len);
                    }
                }

                // After receiving, try to refill pipeline (ET mode: EPOLLOUT won't re-fire)
                TrySend(conn);
            }

            // ── Send (initial EPOLLOUT on connect, or buffer drained) ──
            if (events[i].events & EPOLLOUT) {
                TrySend(conn);
            }

            // ── Error ──
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                conn->connected = false;
            }
        }
    }

    // Count alive connections
    for (auto& c : conns) {
        if (c.connected && c.fd >= 0)
            result.alive_conns++;
        if (c.fd >= 0)
            close(c.fd);
    }

    close(epfd);
}

// ── Statistics ───────────────────────────────────────────────────

static void PrintResults(const Config& cfg,
                         const std::vector<WorkerResult>& results,
                         double elapsed_sec)
{
    // Merge all RTT samples
    size_t total_samples = 0;
    for (auto& r : results)
        total_samples += r.rtt_ns.size();

    std::vector<uint64_t> all_rtt;
    all_rtt.reserve(total_samples);
    for (auto& r : results)
        all_rtt.insert(all_rtt.end(), r.rtt_ns.begin(), r.rtt_ns.end());

    std::sort(all_rtt.begin(), all_rtt.end());

    int alive = 0;
    int total_conns = cfg.threads * cfg.conns;
    for (auto& r : results)
        alive += r.alive_conns;

    std::printf("\n=== Echo Benchmark");
    if (cfg.label[0] != '\0')
        std::printf(" [%s]", cfg.label);
    std::printf(" ===\n");

    std::printf("Config: %d threads x %d conns = %d total, payload=%dB, pipeline=%d\n",
                cfg.threads, cfg.conns, total_conns, cfg.payload, cfg.pipeline);
    std::printf("Duration: %.1fs (warmup: %ds)\n\n", elapsed_sec, cfg.warmup);

    std::printf("--- Results ---\n");
    std::printf("Total echoes:    %zu\n", all_rtt.size());

    if (elapsed_sec > 0) {
        double throughput = static_cast<double>(all_rtt.size()) / elapsed_sec;
        std::printf("Throughput:      %.0f echo/s\n", throughput);
    }

    std::printf("Connections:     %d/%d alive\n", alive, total_conns);

    if (!all_rtt.empty()) {
        auto percentile = [&](double p) -> uint64_t {
            size_t idx = static_cast<size_t>(p * static_cast<double>(all_rtt.size() - 1));
            return all_rtt[idx];
        };

        double avg = 0;
        for (auto v : all_rtt) avg += static_cast<double>(v);
        avg /= static_cast<double>(all_rtt.size());

        std::printf("\nRTT (us):\n");
        std::printf("  min:     %lu\n", all_rtt.front() / 1000);
        std::printf("  p50:     %lu\n", percentile(0.50) / 1000);
        std::printf("  p95:     %lu\n", percentile(0.95) / 1000);
        std::printf("  p99:     %lu\n", percentile(0.99) / 1000);
        std::printf("  p999:    %lu\n", percentile(0.999) / 1000);
        std::printf("  max:     %lu\n", all_rtt.back() / 1000);
        std::printf("  avg:     %.0f\n", avg / 1000.0);
    } else {
        std::printf("\nNo echo responses received.\n");
    }

    std::printf("\n");
}

// ── CLI parsing ──────────────────────────────────────────────────

static Config ParseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        auto match = [&](const char* name) {
            return std::strcmp(argv[i], name) == 0 && i + 1 < argc;
        };

        if (match("--host"))       cfg.host     = argv[++i];
        else if (match("--port"))  cfg.port     = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (match("--threads")) cfg.threads = std::atoi(argv[++i]);
        else if (match("--conns")) cfg.conns    = std::atoi(argv[++i]);
        else if (match("--payload")) cfg.payload = std::atoi(argv[++i]);
        else if (match("--duration")) cfg.duration = std::atoi(argv[++i]);
        else if (match("--warmup")) cfg.warmup  = std::atoi(argv[++i]);
        else if (match("--pipeline")) cfg.pipeline = std::atoi(argv[++i]);
        else if (match("--label")) cfg.label    = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: echo_bench [options]\n"
                        "  --host HOST        Server address (default: 127.0.0.1)\n"
                        "  --port PORT        Server port (default: 9100)\n"
                        "  --threads N        Worker threads (default: 4)\n"
                        "  --conns N          Connections per thread (default: 1)\n"
                        "  --payload N        Packet body size bytes (default: 64, min: 12)\n"
                        "  --duration SECS    Measurement time (default: 10)\n"
                        "  --warmup SECS      Warmup time (default: 2)\n"
                        "  --pipeline N       Max in-flight per conn (default: 1)\n"
                        "  --label TEXT       Result label (default: \"\")\n");
            std::exit(0);
        }
    }

    // Enforce minimum payload
    if (cfg.payload < static_cast<int>(BODY_MIN))
        cfg.payload = static_cast<int>(BODY_MIN);

    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.conns < 1) cfg.conns = 1;
    if (cfg.pipeline < 1) cfg.pipeline = 1;
    if (cfg.duration < 1) cfg.duration = 1;
    if (cfg.warmup < 0) cfg.warmup = 0;

    return cfg;
}

// ── Main ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    Config cfg = ParseArgs(argc, argv);

    int total_conns = cfg.threads * cfg.conns;
    std::printf("echo_bench: %s:%d, %d threads x %d conns = %d, "
                "payload=%d, pipeline=%d, duration=%ds, warmup=%ds\n",
                cfg.host, cfg.port, cfg.threads, cfg.conns, total_conns,
                cfg.payload, cfg.pipeline, cfg.duration, cfg.warmup);

    std::atomic<bool> running{true};
    std::atomic<bool> measuring{false};

    std::vector<WorkerResult> results(cfg.threads);
    std::vector<std::thread> workers;
    workers.reserve(cfg.threads);

    // Launch worker threads
    for (int i = 0; i < cfg.threads; ++i) {
        workers.emplace_back(WorkerThread,
                             std::cref(cfg),
                             std::ref(running),
                             std::ref(measuring),
                             std::ref(results[i]));
    }

    // Warmup phase
    if (cfg.warmup > 0) {
        std::printf("Warming up for %ds...\n", cfg.warmup);
        std::this_thread::sleep_for(std::chrono::seconds(cfg.warmup));
    }

    // Measurement phase
    std::printf("Measuring for %ds...\n", cfg.duration);
    measuring.store(true, std::memory_order_relaxed);

    auto measure_start = Clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(cfg.duration));
    auto measure_end = Clock::now();

    // Stop
    running.store(false, std::memory_order_relaxed);
    for (auto& t : workers)
        t.join();

    double elapsed = std::chrono::duration<double>(measure_end - measure_start).count();
    PrintResults(cfg, results, elapsed);

    return 0;
}
