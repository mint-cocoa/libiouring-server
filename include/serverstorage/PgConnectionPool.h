#pragma once
#include <serverstorage/PgWorkerPool.h>
#include <serverstorage/StorageConfig.h>

#include <functional>
#include <string>
#include <vector>

namespace serverstorage {

// Facade over PgWorkerPool. Legacy name retained for API compatibility.
//
// Queries are dispatched to a dedicated blocking worker thread pool; result
// callbacks are posted back to the originating IoWorker thread via
// IoRing::Post, so callers never need to worry about cross-thread
// synchronization of game-logic state.
class PgConnectionPool {
public:
    explicit PgConnectionPool(const StorageConfig& config);

    // Start the worker pool (blocking connect of all backend connections
    // and spawning of worker threads). The callback is invoked
    // synchronously from the caller thread once startup finishes; it may
    // be null. No async handshake — this is a simple success/failure.
    void Initialize(std::function<void(bool)> cb = nullptr);

    void Execute(std::string sql,
                 std::vector<std::string> params,
                 QueryCallback cb);
    void Execute(std::string sql, QueryCallback cb);

    std::size_t IdleCount() const;
    std::size_t TotalCount() const;

private:
    PgWorkerPool worker_pool_;
};

} // namespace serverstorage
