#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace serverweb::auth {

struct AuthConfig {
    std::string jwt_secret;
    std::chrono::seconds access_token_ttl{900};
    std::chrono::seconds refresh_token_ttl{604800};
};

struct AuthToken {
    std::string access_token;
    std::string refresh_token;
    std::int64_t expires_in;
};

} // namespace serverweb::auth
