#include "redis_client.h"

#include <hiredis.h>
#include <spdlog/spdlog.h>

#include <chrono>

namespace quarto {

RedisClient::RedisClient(const std::string& url) {
    std::string host = "127.0.0.1";
    int port = 6379;

    auto pos = url.find("://");
    if (pos != std::string::npos) {
        auto hostport = url.substr(pos + 3);
        auto colon = hostport.find(':');
        if (colon != std::string::npos) {
            host = hostport.substr(0, colon);
            port = std::stoi(hostport.substr(colon + 1));
        } else {
            host = hostport;
        }
    }

    struct timeval timeout = {2, 0};
    ctx_ = redisConnectWithTimeout(host.c_str(), port, timeout);
    if (ctx_ == nullptr || ctx_->err) {
        std::string err = ctx_ ? ctx_->errstr : "allocation failed";
        spdlog::error("[Redis] Connection failed: {}", err);
        if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }
    } else {
        spdlog::info("[Redis] Connected to {}:{}", host, port);
    }
}

RedisClient::~RedisClient() {
    if (ctx_) redisFree(ctx_);
}

bool RedisClient::IsConnected() const {
    return ctx_ != nullptr && ctx_->err == 0;
}

void RedisClient::Set(const std::string& key, const std::string& value) {
    if (!IsConnected()) return;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %s", key.c_str(), value.c_str()));
    if (reply) freeReplyObject(reply);
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
    if (!IsConnected()) return std::nullopt;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "GET %s", key.c_str()));
    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

void RedisClient::Del(const std::string& key) {
    if (!IsConnected()) return;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "DEL %s", key.c_str()));
    if (reply) freeReplyObject(reply);
}

void RedisClient::TouchActivity(const std::string& user_id) {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Set("activity:" + user_id, std::to_string(now));
}

std::optional<double> RedisClient::GetLastActivity(const std::string& user_id) {
    auto val = Get("activity:" + user_id);
    if (!val) return std::nullopt;
    return std::stod(*val);
}

} // namespace quarto
