#include <serverstorage/PgResult.h>
#include <libpq-fe.h>
#include <cstdlib>
#include <utility>

namespace serverstorage {

PgResult::PgResult(PGresult* res) : res_(res) {}

PgResult::~PgResult() {
    if (res_) PQclear(res_);
}

PgResult::PgResult(PgResult&& other) noexcept : res_(std::exchange(other.res_, nullptr)) {}

PgResult& PgResult::operator=(PgResult&& other) noexcept {
    if (this != &other) {
        if (res_) PQclear(res_);
        res_ = std::exchange(other.res_, nullptr);
    }
    return *this;
}

bool PgResult::IsOk() const {
    return res_ && PQresultStatus(res_) == PGRES_TUPLES_OK;
}

bool PgResult::IsCommand() const {
    return res_ && PQresultStatus(res_) == PGRES_COMMAND_OK;
}

std::string_view PgResult::ErrorMessage() const {
    if (!res_) return "no result";
    const char* msg = PQresultErrorMessage(res_);
    return msg ? msg : "";
}

int PgResult::RowCount() const {
    return res_ ? PQntuples(res_) : 0;
}

int PgResult::ColumnCount() const {
    return res_ ? PQnfields(res_) : 0;
}

std::string_view PgResult::GetString(int row, int col) const {
    if (!res_ || PQgetisnull(res_, row, col)) return {};
    return PQgetvalue(res_, row, col);
}

int PgResult::GetInt(int row, int col) const {
    auto sv = GetString(row, col);
    return sv.empty() ? 0 : std::atoi(sv.data());
}

std::int64_t PgResult::GetInt64(int row, int col) const {
    auto sv = GetString(row, col);
    return sv.empty() ? 0 : std::atoll(sv.data());
}

bool PgResult::IsNull(int row, int col) const {
    return !res_ || PQgetisnull(res_, row, col);
}

} // namespace serverstorage
