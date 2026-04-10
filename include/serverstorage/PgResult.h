#pragma once
#include <cstdint>
#include <string>
#include <string_view>

struct pg_result;
typedef struct pg_result PGresult;

namespace serverstorage {

class PgResult {
public:
    PgResult() = default;
    explicit PgResult(PGresult* res);
    ~PgResult();

    PgResult(PgResult&& other) noexcept;
    PgResult& operator=(PgResult&& other) noexcept;
    PgResult(const PgResult&) = delete;
    PgResult& operator=(const PgResult&) = delete;

    bool IsOk() const;
    bool IsCommand() const;
    std::string_view ErrorMessage() const;

    int RowCount() const;
    int ColumnCount() const;

    std::string_view GetString(int row, int col) const;
    int GetInt(int row, int col) const;
    std::int64_t GetInt64(int row, int col) const;
    bool IsNull(int row, int col) const;

    explicit operator bool() const { return IsOk() || IsCommand(); }

private:
    PGresult* res_ = nullptr;
};

} // namespace serverstorage
