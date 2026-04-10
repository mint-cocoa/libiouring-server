#include <serverstorage/PgConnection.h>

#include <libpq-fe.h>
#include <spdlog/spdlog.h>

#include <string>

namespace serverstorage {

PgConnection::PgConnection(StorageConfig config)
    : config_(std::move(config)) {}

PgConnection::~PgConnection() {
    if (conn_) PQfinish(conn_);
}

bool PgConnection::Connect() {
    conn_ = PQconnectdb(config_.connection_string.c_str());
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        spdlog::error("PgConnection::Connect: {}",
                      conn_ ? PQerrorMessage(conn_) : "null conn");
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
        return false;
    }
    return true;
}

PgResult PgConnection::Execute(std::string_view sql,
                                std::span<const char* const> params) {
    if (!conn_) return PgResult{};

    // PQexec/PQexecParams require null-terminated strings.
    std::string sql_buf(sql);

    PGresult* res;
    if (params.empty()) {
        res = PQexec(conn_, sql_buf.c_str());
    } else {
        res = PQexecParams(conn_, sql_buf.c_str(),
                           static_cast<int>(params.size()),
                           nullptr,             // param types — inferred
                           params.data(),
                           nullptr, nullptr,    // lengths, formats
                           0);                  // text result
    }
    return PgResult{res};
}

} // namespace serverstorage
