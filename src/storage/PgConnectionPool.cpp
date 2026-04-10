#include <serverstorage/PgConnectionPool.h>

#include <utility>

namespace serverstorage {

PgConnectionPool::PgConnectionPool(const StorageConfig& config)
    : worker_pool_(config) {}

void PgConnectionPool::Initialize(std::function<void(bool)> cb) {
    bool ok = worker_pool_.Start();
    if (cb) cb(ok);
}

void PgConnectionPool::Execute(std::string sql,
                                std::vector<std::string> params,
                                QueryCallback cb) {
    worker_pool_.Submit(std::move(sql), std::move(params), std::move(cb));
}

void PgConnectionPool::Execute(std::string sql, QueryCallback cb) {
    worker_pool_.Submit(std::move(sql), {}, std::move(cb));
}

std::size_t PgConnectionPool::IdleCount() const {
    // Pre-refactor semantics reported per-connection idleness; under the
    // worker-pool model each worker is either waiting on the condvar or
    // executing a job, so an accurate instantaneous idle count would
    // require extra bookkeeping for marginal value. Report the worker
    // count as an upper bound — existing callers only use this for
    // logging / rough health checks.
    return worker_pool_.WorkerCount();
}

std::size_t PgConnectionPool::TotalCount() const {
    return worker_pool_.WorkerCount();
}

} // namespace serverstorage
