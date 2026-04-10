#include <serverstorage/PgWorkerPool.h>

#include <servercore/ring/IoRing.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace serverstorage {

PgWorkerPool::PgWorkerPool(StorageConfig config)
    : config_(std::move(config)) {}

PgWorkerPool::~PgWorkerPool() {
    Stop();
}

bool PgWorkerPool::Start() {
    const auto count = config_.connections_per_worker;
    if (count == 0) {
        spdlog::error("PgWorkerPool::Start: connections_per_worker must be >= 1");
        return false;
    }

    // 1. Open all backend connections synchronously up-front. If any
    //    fail, tear down and report failure — partial pools are not
    //    supported.
    conns_.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        auto conn = std::make_unique<PgConnection>(config_);
        if (!conn->Connect()) {
            spdlog::error("PgWorkerPool::Start: worker {} failed to connect", i);
            conns_.clear();
            return false;
        }
        conns_.push_back(std::move(conn));
    }

    // 2. Spawn one worker thread per connection (1:1).
    workers_.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        workers_.emplace_back([this, i] { WorkerLoop(i); });
    }

    spdlog::info("PgWorkerPool: started with {} workers", count);
    return true;
}

void PgWorkerPool::Stop() {
    {
        std::lock_guard lk(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    conns_.clear();
}

void PgWorkerPool::Submit(std::string sql,
                           std::vector<std::string> params,
                           QueryCallback cb) {
    auto* reply = servercore::ring::IoRing::Current();
    if (!reply) {
        spdlog::error("PgWorkerPool::Submit: no IoRing on calling thread; "
                      "cannot deliver callback");
        if (cb) cb(PgResult{});
        return;
    }

    {
        std::lock_guard lk(mutex_);
        if (stopping_) {
            // Reject post-stop submissions; run callback inline with
            // empty result so callers see a deterministic failure.
            if (cb) cb(PgResult{});
            return;
        }
        queue_.push_back(Job{
            std::move(sql),
            std::move(params),
            std::move(cb),
            reply,
        });
    }
    cv_.notify_one();
}

void PgWorkerPool::WorkerLoop(std::size_t idx) {
    auto& conn = *conns_[idx];

    for (;;) {
        Job job;
        {
            std::unique_lock lk(mutex_);
            cv_.wait(lk, [this] {
                return stopping_ || !queue_.empty();
            });
            if (queue_.empty()) {
                // stopping_ must be true here
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        // Build const char* view for libpq. Params are kept alive in
        // job.params for the duration of Execute().
        std::vector<const char*> param_ptrs;
        param_ptrs.reserve(job.params.size());
        for (auto& p : job.params) param_ptrs.push_back(p.c_str());

        PgResult result = conn.Execute(job.sql, param_ptrs);

        if (!job.cb || !job.reply_ring) continue;

        // Hop the result back to the originating IoWorker thread. The
        // lambda captures are all move-only-safe: QueryCallback is
        // move-constructible, PgResult is move-only.
        job.reply_ring->Post(
            [cb = std::move(job.cb),
             result = std::move(result)]() mutable {
                cb(std::move(result));
            });
    }
}

} // namespace serverstorage
