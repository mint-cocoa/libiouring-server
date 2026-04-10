#pragma once
#include <serverstorage/PgConnection.h>
#include <serverstorage/PgResult.h>
#include <serverstorage/StorageConfig.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace servercore::ring { class IoRing; }

namespace serverstorage {

using QueryCallback = std::function<void(PgResult)>;

// Worker pool executing PostgreSQL queries on dedicated blocking threads.
//
// Each worker thread owns exactly one PgConnection (1:1 mapping) and
// processes jobs via synchronous libpq calls. When a job completes, the
// result is posted back to the originating IoWorker via IoRing::Post,
// so the user callback runs on the same thread that called Submit().
//
// This keeps the cross-thread hop to a single bounded queue: DB worker
// thread → originating IoRing::posted_ → IoWorker loop.
class PgWorkerPool {
public:
    explicit PgWorkerPool(StorageConfig config);
    ~PgWorkerPool();

    PgWorkerPool(const PgWorkerPool&) = delete;
    PgWorkerPool& operator=(const PgWorkerPool&) = delete;

    // Blocking connect of all backend connections, then spawn workers.
    // Returns true if every connection succeeded.
    bool Start();

    // Stop all workers, drain in-flight jobs (callbacks discarded), and
    // join threads. Idempotent.
    void Stop();

    // Submit a query. Must be called from a thread with an active IoRing
    // (IoRing::Current() != nullptr); the callback is delivered back to
    // that same IoRing.
    void Submit(std::string sql,
                std::vector<std::string> params,
                QueryCallback cb);

    std::size_t WorkerCount() const noexcept { return workers_.size(); }

private:
    struct Job {
        std::string sql;
        std::vector<std::string> params;
        QueryCallback cb;
        servercore::ring::IoRing* reply_ring = nullptr;
    };

    void WorkerLoop(std::size_t idx);

    StorageConfig config_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    bool stopping_ = false;

    std::vector<std::unique_ptr<PgConnection>> conns_;
    std::vector<std::thread> workers_;
};

} // namespace serverstorage
