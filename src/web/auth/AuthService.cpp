#include <serverweb/AuthService.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace serverweb::auth {

AuthService::AuthService(const AuthConfig& config,
                         serverstorage::PgConnectionPool& db)
    : jwt_(config.jwt_secret), config_(config), db_(db) {}

std::expected<JwtClaims, WebError> AuthService::VerifyToken(
    std::string_view token) const {
    return jwt_.Decode(token);
}

std::string AuthService::GenerateRefreshToken() {
    std::uint8_t buf[32];
    RAND_bytes(buf, sizeof(buf));
    std::ostringstream oss;
    for (auto b : buf)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

AuthToken AuthService::IssueTokens(const std::string& user_id,
                                    const std::string& device_id) {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    JwtClaims claims;
    claims.sub = user_id;
    claims.device_id = device_id;
    claims.iat = now;
    claims.exp = now + config_.access_token_ttl.count();

    return AuthToken{
        .access_token = jwt_.Encode(claims),
        .refresh_token = GenerateRefreshToken(),
        .expires_in = config_.access_token_ttl.count()
    };
}

void AuthService::LoginByDeviceId(
    std::string_view device_id,
    std::function<void(std::expected<AuthToken, WebError>)> cb) {
    auto did = std::string(device_id);

    db_.Execute(
        "SELECT id::text FROM users WHERE device_id = $1",
        {did},
        [this, did, cb = std::move(cb)](serverstorage::PgResult result) mutable {
            if (result && result.RowCount() > 0) {
                auto user_id = std::string(result.GetString(0, 0));
                auto token = IssueTokens(user_id, did);
                auto ttl = std::to_string(config_.refresh_token_ttl.count());
                db_.Execute(
                    "INSERT INTO refresh_tokens (token, user_id, expires_at) "
                    "VALUES ($1, $2::uuid, NOW() + INTERVAL '1 second' * $3)",
                    {token.refresh_token, user_id, ttl},
                    [cb = std::move(cb), token = std::move(token)](
                        serverstorage::PgResult) {
                        cb(std::move(token));
                    });
            } else {
                db_.Execute(
                    "INSERT INTO users (device_id) VALUES ($1) RETURNING id::text",
                    {did},
                    [this, did, cb = std::move(cb)](
                        serverstorage::PgResult result) mutable {
                        if (!result || result.RowCount() == 0) {
                            cb(std::unexpected(WebError::kInternalError));
                            return;
                        }
                        auto user_id = std::string(result.GetString(0, 0));
                        auto token = IssueTokens(user_id, did);
                        auto ttl = std::to_string(config_.refresh_token_ttl.count());
                        db_.Execute(
                            "INSERT INTO refresh_tokens (token, user_id, expires_at) "
                            "VALUES ($1, $2::uuid, NOW() + INTERVAL '1 second' * $3)",
                            {token.refresh_token, user_id, ttl},
                            [cb = std::move(cb), token = std::move(token)](
                                serverstorage::PgResult) {
                                cb(std::move(token));
                            });
                    });
            }
        });
}

void AuthService::RefreshToken(
    std::string_view refresh_token,
    std::function<void(std::expected<AuthToken, WebError>)> cb) {
    auto rt = std::string(refresh_token);

    db_.Execute(
        "DELETE FROM refresh_tokens WHERE token = $1 AND expires_at > NOW() "
        "RETURNING user_id::text",
        {rt},
        [this, cb = std::move(cb)](serverstorage::PgResult result) mutable {
            if (!result || result.RowCount() == 0) {
                cb(std::unexpected(WebError::kUnauthorized));
                return;
            }
            auto user_id = std::string(result.GetString(0, 0));
            auto token = IssueTokens(user_id, "");
            auto ttl = std::to_string(config_.refresh_token_ttl.count());
            db_.Execute(
                "INSERT INTO refresh_tokens (token, user_id, expires_at) "
                "VALUES ($1, $2::uuid, NOW() + INTERVAL '1 second' * $3)",
                {token.refresh_token, user_id, ttl},
                [cb = std::move(cb), token = std::move(token)](
                    serverstorage::PgResult) {
                    cb(std::move(token));
                });
        });
}

void AuthService::InitSchema(std::function<void(bool)> cb) {
    db_.Execute(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),"
        "  device_id VARCHAR(255) UNIQUE,"
        "  username VARCHAR(255),"
        "  metadata JSONB DEFAULT '{}'::jsonb,"
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  updated_at TIMESTAMPTZ DEFAULT NOW()"
        ")",
        [this, cb = std::move(cb)](serverstorage::PgResult result) mutable {
            if (!result) {
                spdlog::error("AuthService::InitSchema users failed: {}",
                              result.ErrorMessage());
                if (cb) cb(false);
                return;
            }
            db_.Execute(
                "CREATE TABLE IF NOT EXISTS refresh_tokens ("
                "  token VARCHAR(255) PRIMARY KEY,"
                "  user_id UUID REFERENCES users(id) ON DELETE CASCADE,"
                "  expires_at TIMESTAMPTZ NOT NULL,"
                "  created_at TIMESTAMPTZ DEFAULT NOW()"
                ")",
                [this, cb = std::move(cb)](
                    serverstorage::PgResult result) mutable {
                    if (!result) {
                        spdlog::error(
                            "AuthService::InitSchema refresh_tokens failed: {}",
                            result.ErrorMessage());
                        if (cb) cb(false);
                        return;
                    }
                    db_.Execute(
                        "CREATE INDEX IF NOT EXISTS idx_refresh_tokens_user_id "
                        "ON refresh_tokens(user_id);"
                        "CREATE INDEX IF NOT EXISTS idx_refresh_tokens_expires_at "
                        "ON refresh_tokens(expires_at)",
                        [cb = std::move(cb)](serverstorage::PgResult result) {
                            if (cb) cb(static_cast<bool>(result));
                        });
                });
        });
}

} // namespace serverweb::auth
