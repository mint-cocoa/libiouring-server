#include "gateway.h"
#include "config.h"
#include "oauth.h"
#include "k8s_client.h"
#include "redis_client.h"

#include <serverweb/WebServer.h>
#include <serverweb/LoggerMiddleware.h>
#include <serverweb/CorsMiddleware.h>
#include <serverweb/CookieAuthMiddleware.h>
#include <serverweb/StaticFiles.h>
#include <serverweb/JwtCodec.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <thread>

#if HAS_CURL
#include <curl/curl.h>
#endif

namespace quarto {

#if HAS_CURL
static size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

// Proxy an HTTP request to an editor pod via libcurl.
// Returns {status_code, response_body}. status_code 0 means connection failure.
static std::pair<int, std::string> ProxyToEditor(
    const std::string& pod_ip, uint16_t port,
    const std::string& method, const std::string& path,
    const std::string& body, const std::string& content_type)
{
    CURL* curl = curl_easy_init();
    if (!curl) return {0, "curl_easy_init failed"};

    std::string url = "http://" + pod_ip + ":" + std::to_string(port) + path;
    std::string response;
    long http_code = 0;

    struct curl_slist* headers = nullptr;
    if (!content_type.empty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    }
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {0, std::string("proxy error: ") + curl_easy_strerror(res)};
    }
    return {static_cast<int>(http_code), response};
}
#endif

// Wait for a pod to get an IP (poll with backoff, up to ~30 seconds).
static std::optional<std::string> WaitForPodIp(K8sClient& k8s, const std::string& user_id) {
    for (int i = 0; i < 15; ++i) {
        auto ip = k8s.GetPodIp(user_id);
        if (ip) return ip;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return std::nullopt;
}

// Background cleanup thread: delete idle editor pods.
static void CleanupLoop(K8sClient& k8s, RedisClient& redis,
                        const CleanupConfig& cfg, std::atomic<bool>& running) {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(cfg.interval));
        if (!running.load()) break;

        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        auto pods = k8s.ListEditorPods();
        for (const auto& pod : pods) {
            if (pod.user_id.empty()) continue;
            auto last = redis.GetLastActivity(pod.user_id);
            if (!last) continue;
            auto idle = now - static_cast<int64_t>(*last);
            if (idle > cfg.idle_timeout) {
                spdlog::info("[Cleanup] Pod {} idle for {}s, deleting", pod.name, idle);
                k8s.DeleteUserPod(pod.user_id);
                redis.Del("activity:" + pod.user_id);
            }
        }
    }
}

int run_gateway(const Config& config) {
    // --- Initialize clients ---
    OAuthClient oauth(config.auth.github_client_id,
                      config.auth.github_client_secret,
                      config.auth.github_redirect_uri);

    K8sClient k8s(config.k8s.namespace_name,
                  config.k8s.editor_image,
                  config.k8s.editor_config);

    RedisClient redis(config.redis.url);

    serverweb::auth::JwtCodec jwt(config.auth.jwt_secret);

    // --- Web server ---
    serverweb::WebServerConfig ws_config;
    ws_config.port = config.server.port;
    ws_config.worker_count = config.server.workers;
    serverweb::WebServer server(ws_config);

    // Middleware
    server.Use(std::make_shared<serverweb::middleware::Logger>());

    server.Use(std::make_shared<serverweb::middleware::Cors>(serverweb::middleware::CorsOptions{
        .allowed_origins = {"*"},
        .allow_credentials = true,
    }));

    server.Use(std::make_shared<serverweb::middleware::CookieAuth>(
        serverweb::middleware::CookieAuthOptions{
            .jwt_secret = config.auth.jwt_secret,
            .cookie_name = "token",
            .exclude_paths = {"/auth/github", "/auth/github/callback", "/health"},
        }));

    // --- Auth routes ---

    // GET /auth/github — redirect to GitHub OAuth
    server.Get("/auth/github", [&oauth]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto url = oauth.GetAuthorizeUrl();
        ctx.response.Status(serverweb::HttpStatus::kFound)
            .Header("Location", url)
            .Body("")
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "OAuth not available (built without libcurl)");
#endif
    });

    // GET /auth/github/callback — exchange code for token, set cookie
    server.Get("/auth/github/callback",
        [&oauth, &jwt, &config]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto code = std::string(ctx.request.QueryParam("code"));
        if (code.empty()) {
            ctx.SendError(serverweb::HttpStatus::kBadRequest, "Missing code parameter");
            return;
        }

        auto result = oauth.ExchangeCode(code);
        if (!result) {
            spdlog::error("[Auth] OAuth exchange failed: {}", result.error());
            ctx.SendError(serverweb::HttpStatus::kUnauthorized, result.error());
            return;
        }

        auto& user = *result;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        serverweb::auth::JwtClaims claims;
        claims.sub = user.login;
        claims.iat = now;
        claims.exp = now + config.auth.jwt_ttl_seconds;

        auto token = jwt.Encode(claims);

        auto cookie = "token=" + token +
            "; HttpOnly; Path=/; SameSite=Lax; Max-Age=" +
            std::to_string(config.auth.jwt_ttl_seconds);

        ctx.response.Status(serverweb::HttpStatus::kFound)
            .Header("Set-Cookie", cookie)
            .Header("Location", config.auth.frontend_url)
            .Body("")
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "OAuth not available (built without libcurl)");
#endif
    });

    // GET /auth/me — return current user info
    server.Get("/auth/me", [](serverweb::RequestContext& ctx) {
        auto& user_id = ctx.request.authenticated_user_id;
        if (user_id.empty()) {
            ctx.SendError(serverweb::HttpStatus::kUnauthorized, "Not authenticated");
            return;
        }
        ctx.SendJson(nlohmann::json{{"user_id", user_id}}.dump());
    });

    // POST /auth/logout — clear cookie
    server.Post("/auth/logout", [](serverweb::RequestContext& ctx) {
        ctx.response.Status(serverweb::HttpStatus::kOk)
            .Header("Set-Cookie", "token=; HttpOnly; Path=/; Max-Age=0")
            .Json(R"({"status":"logged_out"})")
            .Send();
    });

    // --- API proxy routes ---
    // Each route validates the authenticated user, ensures a pod exists,
    // waits for its IP, then proxies the request via libcurl.

    // GET /api/:user_id/documents
    server.Get("/api/:user_id/documents",
        [&k8s, &redis]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto path_user = std::string(ctx.request.Param("user_id"));
        auto& auth_user = ctx.request.authenticated_user_id;
        if (auth_user.empty() || auth_user != path_user) {
            ctx.SendError(serverweb::HttpStatus::kForbidden, "Access denied");
            return;
        }
        redis.TouchActivity(auth_user);
        k8s.CreateUserPod(auth_user);
        auto ip = WaitForPodIp(k8s, auth_user);
        if (!ip) {
            ctx.SendError(serverweb::HttpStatus::kServiceUnavailable, "Editor pod not ready");
            return;
        }
        auto [code, body] = ProxyToEditor(*ip, 8080, "GET", "/documents", "", "");
        if (code == 0) {
            ctx.SendError(serverweb::HttpStatus::kBadGateway, body);
            return;
        }
        ctx.response.Status(static_cast<serverweb::HttpStatus>(code))
            .Json(body)
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "API proxy not available (built without libcurl)");
#endif
    });

    // GET /api/:user_id/documents/:slug
    server.Get("/api/:user_id/documents/:slug",
        [&k8s, &redis]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto path_user = std::string(ctx.request.Param("user_id"));
        auto slug = std::string(ctx.request.Param("slug"));
        auto& auth_user = ctx.request.authenticated_user_id;
        if (auth_user.empty() || auth_user != path_user) {
            ctx.SendError(serverweb::HttpStatus::kForbidden, "Access denied");
            return;
        }
        redis.TouchActivity(auth_user);
        k8s.CreateUserPod(auth_user);
        auto ip = WaitForPodIp(k8s, auth_user);
        if (!ip) {
            ctx.SendError(serverweb::HttpStatus::kServiceUnavailable, "Editor pod not ready");
            return;
        }
        auto [code, body] = ProxyToEditor(*ip, 8080, "GET", "/documents/" + slug, "", "");
        if (code == 0) {
            ctx.SendError(serverweb::HttpStatus::kBadGateway, body);
            return;
        }
        ctx.response.Status(static_cast<serverweb::HttpStatus>(code))
            .Json(body)
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "API proxy not available (built without libcurl)");
#endif
    });

    // POST /api/:user_id/documents/:slug
    server.Post("/api/:user_id/documents/:slug",
        [&k8s, &redis]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto path_user = std::string(ctx.request.Param("user_id"));
        auto slug = std::string(ctx.request.Param("slug"));
        auto& auth_user = ctx.request.authenticated_user_id;
        if (auth_user.empty() || auth_user != path_user) {
            ctx.SendError(serverweb::HttpStatus::kForbidden, "Access denied");
            return;
        }
        redis.TouchActivity(auth_user);
        k8s.CreateUserPod(auth_user);
        auto ip = WaitForPodIp(k8s, auth_user);
        if (!ip) {
            ctx.SendError(serverweb::HttpStatus::kServiceUnavailable, "Editor pod not ready");
            return;
        }
        auto ct = std::string(ctx.request.ContentType());
        auto [code, body] = ProxyToEditor(*ip, 8080, "POST",
            "/documents/" + slug, ctx.request.body, ct);
        if (code == 0) {
            ctx.SendError(serverweb::HttpStatus::kBadGateway, body);
            return;
        }
        ctx.response.Status(static_cast<serverweb::HttpStatus>(code))
            .Json(body)
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "API proxy not available (built without libcurl)");
#endif
    });

    // POST /api/:user_id/publish/:slug
    server.Post("/api/:user_id/publish/:slug",
        [&k8s, &redis]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto path_user = std::string(ctx.request.Param("user_id"));
        auto slug = std::string(ctx.request.Param("slug"));
        auto& auth_user = ctx.request.authenticated_user_id;
        if (auth_user.empty() || auth_user != path_user) {
            ctx.SendError(serverweb::HttpStatus::kForbidden, "Access denied");
            return;
        }
        redis.TouchActivity(auth_user);
        k8s.CreateUserPod(auth_user);
        auto ip = WaitForPodIp(k8s, auth_user);
        if (!ip) {
            ctx.SendError(serverweb::HttpStatus::kServiceUnavailable, "Editor pod not ready");
            return;
        }
        auto [code, body] = ProxyToEditor(*ip, 8080, "POST",
            "/publish/" + slug, ctx.request.body, "application/json");
        if (code == 0) {
            ctx.SendError(serverweb::HttpStatus::kBadGateway, body);
            return;
        }
        ctx.response.Status(static_cast<serverweb::HttpStatus>(code))
            .Json(body)
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "API proxy not available (built without libcurl)");
#endif
    });

    // GET /api/:user_id/publish
    server.Get("/api/:user_id/publish",
        [&k8s, &redis]([[maybe_unused]] serverweb::RequestContext& ctx) {
#if HAS_CURL
        auto path_user = std::string(ctx.request.Param("user_id"));
        auto& auth_user = ctx.request.authenticated_user_id;
        if (auth_user.empty() || auth_user != path_user) {
            ctx.SendError(serverweb::HttpStatus::kForbidden, "Access denied");
            return;
        }
        redis.TouchActivity(auth_user);
        k8s.CreateUserPod(auth_user);
        auto ip = WaitForPodIp(k8s, auth_user);
        if (!ip) {
            ctx.SendError(serverweb::HttpStatus::kServiceUnavailable, "Editor pod not ready");
            return;
        }
        auto [code, body] = ProxyToEditor(*ip, 8080, "GET", "/publish", "", "");
        if (code == 0) {
            ctx.SendError(serverweb::HttpStatus::kBadGateway, body);
            return;
        }
        ctx.response.Status(static_cast<serverweb::HttpStatus>(code))
            .Json(body)
            .Send();
#else
        ctx.SendError(serverweb::HttpStatus::kNotImplemented,
                      "API proxy not available (built without libcurl)");
#endif
    });

    // --- Health ---
    server.Get("/health", [&redis](serverweb::RequestContext& ctx) {
        nlohmann::json j;
        j["status"] = "ok";
        j["mode"] = "gateway";
        j["redis"] = redis.IsConnected() ? "connected" : "disconnected";
        ctx.SendJson(j.dump());
    });

    // --- Static files ---
    server.Use(std::make_shared<serverweb::middleware::StaticFiles>(
        serverweb::middleware::StaticFilesOptions{
            .root = "/srv/frontend",
            .prefix = "/",
            .index_files = {"index.html"},
        }));

    // --- Cleanup thread ---
    std::atomic<bool> cleanup_running{true};
    std::thread cleanup_thread(CleanupLoop,
        std::ref(k8s), std::ref(redis), std::ref(config.cleanup),
        std::ref(cleanup_running));

    spdlog::info("[Gateway] Starting on port {}", config.server.port);
    server.Start();

    // Shutdown
    cleanup_running.store(false);
    if (cleanup_thread.joinable()) {
        cleanup_thread.join();
    }

    return 0;
}

} // namespace quarto
