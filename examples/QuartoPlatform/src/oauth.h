// examples/QuartoPlatform/src/oauth.h
#pragma once

#include <expected>
#include <string>

#include <nlohmann/json.hpp>

namespace quarto {

struct GitHubUser {
    std::string login;
    int id = 0;
    std::string avatar_url;
};

class OAuthClient {
public:
    OAuthClient(const std::string& client_id,
                const std::string& client_secret,
                const std::string& redirect_uri);

    // OAuth authorize URL を生成
    std::string GetAuthorizeUrl() const;

    // code -> access_token -> GitHubUser
    std::expected<GitHubUser, std::string>
        ExchangeCode(const std::string& code) const;

private:
    // HTTP helpers (will use libcurl when available)
    static std::expected<std::string, std::string>
        HttpPost(const std::string& url, const std::string& body,
                 const std::string& accept = "application/json");
    static std::expected<std::string, std::string>
        HttpGet(const std::string& url, const std::string& auth_header);

    std::string client_id_;
    std::string client_secret_;
    std::string redirect_uri_;
};

} // namespace quarto
