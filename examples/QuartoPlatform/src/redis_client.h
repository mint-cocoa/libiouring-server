#pragma once

#include <optional>
#include <string>

struct redisContext;

namespace quarto {

class RedisClient {
public:
    explicit RedisClient(const std::string& url);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool IsConnected() const;

    void Set(const std::string& key, const std::string& value);
    std::optional<std::string> Get(const std::string& key);
    void Del(const std::string& key);

    void TouchActivity(const std::string& user_id);
    std::optional<double> GetLastActivity(const std::string& user_id);

private:
    redisContext* ctx_ = nullptr;
};

} // namespace quarto
