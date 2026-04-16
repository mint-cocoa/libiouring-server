#include "config.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <stdexcept>
#include <regex>
#include <cstdlib>

namespace quarto {

static std::string ExpandEnv(const std::string& value) {
    // Supports ${VAR} and ${VAR:-default}
    static const std::regex env_re(R"(\$\{(\w+)(?::-([^}]*))?\})");
    std::string result = value;
    std::smatch match;
    while (std::regex_search(result, match, env_re)) {
        const char* env_val = std::getenv(match[1].str().c_str());
        std::string replacement;
        if (env_val) {
            replacement = env_val;
        } else if (match[2].matched) {
            replacement = match[2].str();
        }
        result = match.prefix().str() + replacement + match.suffix().str();
    }
    return result;
}

static std::string GetStr(const YAML::Node& node, const std::string& key,
                          const std::string& default_val = "") {
    if (node[key]) return ExpandEnv(node[key].as<std::string>());
    return default_val;
}

static int GetInt(const YAML::Node& node, const std::string& key, int default_val = 0) {
    if (!node[key]) return default_val;
    // Support env var expansion in integer fields (e.g., port: "${SERVER_PORT:-8080}")
    try {
        return node[key].as<int>();
    } catch (...) {
        auto expanded = ExpandEnv(node[key].as<std::string>());
        return expanded.empty() ? default_val : std::stoi(expanded);
    }
}

static bool GetBool(const YAML::Node& node, const std::string& key, bool default_val = false) {
    if (node[key]) return node[key].as<bool>();
    return default_val;
}

Config Config::Load(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load config: " + std::string(e.what()));
    }

    Config cfg;
    cfg.mode = GetStr(root, "mode", "gateway");

    if (auto s = root["server"]) {
        cfg.server.port = static_cast<std::uint16_t>(GetInt(s, "port", 8080));
        cfg.server.workers = GetInt(s, "workers", 2);
    }

    if (auto a = root["auth"]) {
        cfg.auth.jwt_secret = GetStr(a, "jwt_secret");
        cfg.auth.github_client_id = GetStr(a, "github_client_id");
        cfg.auth.github_client_secret = GetStr(a, "github_client_secret");
        cfg.auth.github_redirect_uri = GetStr(a, "github_redirect_uri");
        cfg.auth.frontend_url = GetStr(a, "frontend_url");
        cfg.auth.jwt_ttl_seconds = GetInt(a, "jwt_ttl_seconds", 86400);
    }

    if (auto p = root["proxy"]) {
        if (auto routes = p["routes"]) {
            for (const auto& r : routes) {
                ProxyRoute route;
                route.path_prefix = GetStr(r, "path_prefix");
                route.upstream_host = GetStr(r, "upstream_host");
                route.upstream_port = static_cast<std::uint16_t>(GetInt(r, "upstream_port", 8080));
                route.websocket = GetBool(r, "websocket");
                cfg.proxy.routes.push_back(std::move(route));
            }
        }
    }

    if (auto ss = root["static_sites"]) {
        for (const auto& s : ss) {
            StaticSiteConfig site;
            site.root = GetStr(s, "root");
            site.spa = GetBool(s, "spa");
            cfg.static_sites.push_back(std::move(site));
        }
    }

    if (auto r = root["redis"]) {
        cfg.redis.url = GetStr(r, "url", "redis://localhost:6379");
    }

    if (auto k = root["k8s"]) {
        cfg.k8s.namespace_name = GetStr(k, "namespace", "quarto");
        cfg.k8s.editor_image = GetStr(k, "editor_image");
        cfg.k8s.editor_config = GetStr(k, "editor_config");
    }

    if (auto c = root["cleanup"]) {
        cfg.cleanup.idle_timeout = GetInt(c, "idle_timeout", 1800);
        cfg.cleanup.interval = GetInt(c, "interval", 300);
    }

    if (auto e = root["editor"]) {
        cfg.editor.workspace = GetStr(e, "workspace", "/workspace");
        cfg.editor.published = GetStr(e, "published", "/published");
        cfg.editor.user_id = GetStr(e, "user_id");
        cfg.editor.preview_port = static_cast<std::uint16_t>(GetInt(e, "preview_port", 4000));
        cfg.editor.static_dir = GetStr(e, "static_dir");
    }

    if (auto q = root["quarto"]) {
        cfg.quarto.render_profile = GetStr(q, "render_profile", "production");
        cfg.quarto.timeout = GetInt(q, "timeout", 120);
    }

    return cfg;
}

} // namespace quarto
