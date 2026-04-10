#include <servercore/ring/IoRing.h>
#include <servercore/io/Session.h>
#include <servercore/io/Listener.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/buffer/RecvBuffer.h>
#include <servercore/Types.h>

#include <packet_framing.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace servercore;
using namespace servercore::ring;
using namespace servercore::io;
using namespace servercore::buffer;

static std::atomic<bool> g_running{true};

static void SignalHandler(int) { g_running = false; }

// ── EchoSession ────────────────────────────────────────────────────────────

class EchoSession : public Session {
public:
    EchoSession(int fd, IoRing& ring, BufferPool& pool)
        : Session(fd, ring, pool) {}

protected:
    void OnRecv(std::span<const std::byte> data) override {
        bool ok = bench::DrainPackets(
            recv_buf_, data,
            [this](uint16_t msg_id, const std::byte* body, uint32_t body_len) {
                // Reconstruct full packet: [size:2][id:2][body...]
                uint32_t pkt_size = bench::kHeaderSize + body_len;
                auto alloc = Pool().Allocate(pkt_size);
                if (!alloc) return;
                SendBufferRef buf = std::move(*alloc);

                auto writable = buf->Writable();
                auto* dst = writable.data();

                // Write header (little-endian)
                uint16_t size_le = static_cast<uint16_t>(pkt_size);
                std::memcpy(dst,     &size_le, 2);
                std::memcpy(dst + 2, &msg_id,  2);

                // Write body
                std::memcpy(dst + bench::kHeaderSize, body, body_len);
                buf->Commit(pkt_size);

                Send(std::move(buf));
            });

        if (!ok) {
            Disconnect();
        }
    }

private:
    RecvBuffer recv_buf_;
};

// ── Worker thread ──────────────────────────────────────────────────────────

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

    SessionFactory factory = [](int fd, IoRing& r,
                                BufferPool& p,
                                ContextId ctx) -> SessionRef {
        auto sess = std::make_shared<EchoSession>(fd, r, p);
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

    while (g_running) {
        ring->ProcessPostedTasks();
        ring->Dispatch(std::chrono::milliseconds{10});
    }

    listener->Stop();
}

// ── main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    uint16_t port    = 9100;
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

    std::printf("EchoServer starting on port %u with %d threads\n", port, threads);

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(RunWorker, i, port);
    }

    for (auto& w : workers) {
        w.join();
    }

    std::printf("EchoServer stopped.\n");
    return 0;
}
