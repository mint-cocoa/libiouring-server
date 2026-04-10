#pragma once
#include <serverstorage/PgResult.h>
#include <serverstorage/StorageConfig.h>

#include <span>
#include <string>
#include <string_view>

struct pg_conn;
typedef struct pg_conn PGconn;

namespace serverstorage {

// Blocking PostgreSQL connection wrapper around libpq.
//
// Owned exclusively by a PgWorkerPool worker thread. All operations are
// synchronous (PQconnectdb / PQexec / PQexecParams); there is no
// readiness polling, no io_uring integration, no shared state.
class PgConnection {
public:
    explicit PgConnection(StorageConfig config);
    ~PgConnection();

    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;
    PgConnection(PgConnection&&) = delete;
    PgConnection& operator=(PgConnection&&) = delete;

    // Blocking connect. Returns true on success.
    bool Connect();

    // Blocking query execution. Caller must keep params alive for the
    // duration of the call.
    PgResult Execute(std::string_view sql,
                     std::span<const char* const> params);

    bool IsConnected() const noexcept { return conn_ != nullptr; }

private:
    StorageConfig config_;
    PGconn* conn_ = nullptr;
};

} // namespace serverstorage
