#pragma once
#include <serverstorage/PgConnectionPool.h>
#include <cstdint>
#include <string>
#include <vector>

namespace serverstorage {

class QueryBuilder {
public:
    QueryBuilder& Sql(std::string sql) {
        sql_ = std::move(sql);
        return *this;
    }

    QueryBuilder& Param(std::string value) {
        params_.push_back(std::move(value));
        return *this;
    }

    QueryBuilder& Param(int value) {
        params_.push_back(std::to_string(value));
        return *this;
    }

    QueryBuilder& Param(std::int64_t value) {
        params_.push_back(std::to_string(value));
        return *this;
    }

    void Execute(PgConnectionPool& pool, QueryCallback cb) {
        pool.Execute(std::move(sql_), std::move(params_), std::move(cb));
    }

private:
    std::string sql_;
    std::vector<std::string> params_;
};

} // namespace serverstorage
