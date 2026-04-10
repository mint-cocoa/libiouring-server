#pragma once
#include <serverweb/AuthConfig.h>
#include <serverweb/Error.h>
#include <serverweb/JwtCodec.h>
#include <serverstorage/PgConnectionPool.h>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace serverweb::auth {

class AuthService {
public:
    AuthService(const AuthConfig& config, serverstorage::PgConnectionPool& db);

    void LoginByDeviceId(std::string_view device_id,
                         std::function<void(std::expected<AuthToken, WebError>)> cb);

    std::expected<JwtClaims, WebError> VerifyToken(std::string_view token) const;

    void RefreshToken(std::string_view refresh_token,
                      std::function<void(std::expected<AuthToken, WebError>)> cb);

    void InitSchema(std::function<void(bool)> cb);

private:
    AuthToken IssueTokens(const std::string& user_id,
                          const std::string& device_id);
    static std::string GenerateRefreshToken();

    JwtCodec jwt_;
    AuthConfig config_;
    serverstorage::PgConnectionPool& db_;
};

} // namespace serverweb::auth
