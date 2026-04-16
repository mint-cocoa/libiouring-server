#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace quarto {

struct ServerConfig {
    std::uint16_t port = 8080;
    int workers = 2;
};

struct AuthConfig {
    std::string jwt_secret;
    std::string github_client_id;
    std::string github_client_secret;
    std::string github_redirect_uri;
    std::string frontend_url;
    int jwt_ttl_seconds = 86400;
};

struct ProxyRoute {
    std::string path_prefix;
    std::string upstream_host;
    std::uint16_t upstream_port = 8080;
    bool websocket = false;
};

struct ProxyConfig {
    std::vector<ProxyRoute> routes;
};

struct StaticSiteConfig {
    std::string root;
    bool spa = false;
};

struct RedisConfig {
    std::string url = "redis://localhost:6379";
};

struct K8sConfig {
    std::string namespace_name = "quarto";
    std::string editor_image;
    std::string editor_config;
};

struct CleanupConfig {
    int idle_timeout = 1800;
    int interval = 300;
};

struct EditorModeConfig {
    std::string workspace = "/workspace";
    std::string published = "/published";
    std::string user_id;
    std::uint16_t preview_port = 4000;
    std::string static_dir;  // frontend dist/ path; empty = no static serving
};

struct QuartoConfig {
    std::string render_profile = "production";
    int timeout = 120;
};

struct Config {
    std::string mode;
    ServerConfig server;
    AuthConfig auth;
    ProxyConfig proxy;
    std::vector<StaticSiteConfig> static_sites;
    RedisConfig redis;
    K8sConfig k8s;
    CleanupConfig cleanup;
    EditorModeConfig editor;
    QuartoConfig quarto;

    static Config Load(const std::string& path);
};

} // namespace quarto
