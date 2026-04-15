// examples/QuartoPlatform/src/oauth.cpp
#include "oauth.h"

#include <spdlog/spdlog.h>

#if HAS_CURL
#include <curl/curl.h>
#endif

namespace quarto {

#if HAS_CURL
static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}
#endif

OAuthClient::OAuthClient(const std::string& client_id,
                         const std::string& client_secret,
                         const std::string& redirect_uri)
    : client_id_(client_id)
    , client_secret_(client_secret)
    , redirect_uri_(redirect_uri) {}

std::string OAuthClient::GetAuthorizeUrl() const {
    return "https://github.com/login/oauth/authorize"
           "?client_id=" + client_id_ +
           "&redirect_uri=" + redirect_uri_ +
           "&scope=read:user";
}

std::expected<GitHubUser, std::string>
OAuthClient::ExchangeCode(const std::string& code) const {
    // Step 1: code -> access_token
    nlohmann::json body;
    body["client_id"] = client_id_;
    body["client_secret"] = client_secret_;
    body["code"] = code;

    auto token_resp = HttpPost(
        "https://github.com/login/oauth/access_token",
        body.dump());
    if (!token_resp) return std::unexpected(token_resp.error());

    auto token_json = nlohmann::json::parse(*token_resp, nullptr, false);
    if (token_json.is_discarded() || !token_json.contains("access_token")) {
        return std::unexpected("GitHub OAuth: no access_token in response");
    }
    auto access_token = token_json["access_token"].get<std::string>();

    // Step 2: access_token -> user info
    auto user_resp = HttpGet(
        "https://api.github.com/user",
        "Bearer " + access_token);
    if (!user_resp) return std::unexpected(user_resp.error());

    auto user_json = nlohmann::json::parse(*user_resp, nullptr, false);
    if (user_json.is_discarded() || !user_json.contains("login")) {
        return std::unexpected("GitHub API: invalid user response");
    }

    GitHubUser user;
    user.login = user_json["login"].get<std::string>();
    user.id = user_json.value("id", 0);
    user.avatar_url = user_json.value("avatar_url", "");
    return user;
}

std::expected<std::string, std::string>
OAuthClient::HttpPost(const std::string& url, const std::string& body,
                      const std::string& accept) {
#if HAS_CURL
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected("curl_easy_init failed");

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Accept: " + accept).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return std::unexpected(std::string("curl POST failed: ") + curl_easy_strerror(res));
    }
    return response;
#else
    spdlog::warn("[OAuth] HttpPost not implemented (libcurl not available): {}", url);
    return std::unexpected("HTTP client not available (build without libcurl)");
#endif
}

std::expected<std::string, std::string>
OAuthClient::HttpGet(const std::string& url, const std::string& auth_header) {
#if HAS_CURL
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected("curl_easy_init failed");

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: " + auth_header).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: quarto-server");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return std::unexpected(std::string("curl GET failed: ") + curl_easy_strerror(res));
    }
    return response;
#else
    spdlog::warn("[OAuth] HttpGet not implemented (libcurl not available): {}", url);
    return std::unexpected("HTTP client not available (build without libcurl)");
#endif
}

} // namespace quarto
