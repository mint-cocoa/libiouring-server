#include "editor.h"
#include "quarto.h"

#include <serverweb/WebServer.h>
#include <serverweb/LoggerMiddleware.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

namespace quarto {

namespace fs = std::filesystem;

static nlohmann::json ParseFrontmatter(const std::string& content) {
    nlohmann::json meta;
    meta["title"] = "";
    meta["date"] = "";

    if (content.size() < 3 || content.substr(0, 3) != "---") return meta;
    auto end = content.find("---", 3);
    if (end == std::string::npos) return meta;

    auto yaml_str = content.substr(3, end - 3);
    for (const auto& [prefix, key] : std::vector<std::pair<std::string, std::string>>{
        {"title:", "title"}, {"date:", "date"}
    }) {
        auto pos = yaml_str.find(prefix);
        if (pos != std::string::npos) {
            auto val_start = pos + prefix.size();
            auto val_end = yaml_str.find('\n', val_start);
            auto val = yaml_str.substr(val_start, val_end - val_start);
            while (!val.empty() && (val.front() == ' ' || val.front() == '"' || val.front() == '\''))
                val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '"' || val.back() == '\''))
                val.pop_back();
            meta[key] = val;
        }
    }
    return meta;
}

static std::string ReadFile(const fs::path& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int run_editor(const Config& config) {
    auto workspace = fs::path(config.editor.workspace);
    auto published = fs::path(config.editor.published);
    auto user_id = config.editor.user_id;

    QuartoRunner quarto(
        workspace.string(), published.string(), user_id,
        config.quarto.render_profile, config.quarto.timeout);

    quarto.StartPreview(config.editor.preview_port);

    serverweb::WebServerConfig ws_config;
    ws_config.port = config.server.port;
    ws_config.worker_count = config.server.workers;
    serverweb::WebServer server(ws_config);

    server.Use(std::make_shared<serverweb::middleware::Logger>());

    // Health
    server.Get("/health", [&quarto, &user_id](serverweb::RequestContext& ctx) {
        nlohmann::json j;
        j["status"] = "ok";
        j["user_id"] = user_id;
        j["preview_running"] = quarto.IsPreviewRunning();
        ctx.SendJson(j.dump());
    });

    // List documents
    server.Get("/documents", [&workspace](serverweb::RequestContext& ctx) {
        nlohmann::json docs = nlohmann::json::array();
        if (fs::exists(workspace)) {
            for (const auto& entry : fs::directory_iterator(workspace)) {
                if (entry.path().extension() != ".qmd") continue;
                if (entry.path().filename().string().starts_with("_")) continue;
                auto slug = entry.path().stem().string();
                auto content = ReadFile(entry.path());
                auto meta = ParseFrontmatter(content);
                meta["slug"] = slug;
                docs.push_back(meta);
            }
        }
        std::sort(docs.begin(), docs.end(),
            [](const auto& a, const auto& b) { return a["slug"] < b["slug"]; });
        ctx.SendJson(nlohmann::json{{"documents", docs}}.dump());
    });

    // Get document
    server.Get("/documents/:slug", [&workspace](serverweb::RequestContext& ctx) {
        auto slug = std::string(ctx.request.Param("slug"));
        auto path = workspace / (slug + ".qmd");
        if (!fs::exists(path)) {
            ctx.SendError(serverweb::HttpStatus::kNotFound, "Document not found");
            return;
        }
        auto content = ReadFile(path);
        ctx.SendJson(nlohmann::json{{"slug", slug}, {"content", content}}.dump());
    });

    // Save document
    server.Post("/documents/:slug", [&workspace](serverweb::RequestContext& ctx) {
        auto slug = std::string(ctx.request.Param("slug"));
        auto body = nlohmann::json::parse(ctx.request.body, nullptr, false);
        if (body.is_discarded() || !body.contains("content")) {
            ctx.SendError(serverweb::HttpStatus::kBadRequest, "Missing content field");
            return;
        }
        auto path = workspace / (slug + ".qmd");
        std::ofstream f(path);
        f << body["content"].get<std::string>();
        ctx.SendJson(nlohmann::json{{"slug", slug}, {"saved", true}}.dump());
    });

    // Delete document
    server.Delete("/documents/:slug", [&workspace](serverweb::RequestContext& ctx) {
        auto slug = std::string(ctx.request.Param("slug"));
        auto path = workspace / (slug + ".qmd");
        if (!fs::exists(path)) {
            ctx.SendError(serverweb::HttpStatus::kNotFound, "Document not found");
            return;
        }
        fs::remove(path);
        ctx.SendJson(nlohmann::json{{"slug", slug}, {"deleted", true}}.dump());
    });

    // Publish
    server.Post("/publish/:slug", [&quarto, &user_id](serverweb::RequestContext& ctx) {
        auto slug = std::string(ctx.request.Param("slug"));
        auto result = quarto.RenderProduction(slug);
        if (!result.success) {
            ctx.SendError(serverweb::HttpStatus::kInternalServerError, result.error);
            return;
        }
        auto url_path = "/" + user_id + "/" + slug;
        ctx.SendJson(nlohmann::json{
            {"status", "published"}, {"slug", slug}, {"url_path", url_path}
        }.dump());
    });

    // Unpublish
    server.Delete("/publish/:slug", [&quarto](serverweb::RequestContext& ctx) {
        auto slug = std::string(ctx.request.Param("slug"));
        quarto.RemovePublished(slug);
        ctx.SendJson(nlohmann::json{{"status", "unpublished"}, {"slug", slug}}.dump());
    });

    // List published
    server.Get("/publish", [&published, &user_id](serverweb::RequestContext& ctx) {
        nlohmann::json items = nlohmann::json::array();
        auto user_dir = fs::path(published) / user_id;
        if (fs::exists(user_dir)) {
            for (const auto& entry : fs::directory_iterator(user_dir)) {
                if (!entry.is_directory()) continue;
                if (!fs::exists(entry.path() / "index.html")) continue;
                items.push_back({
                    {"slug", entry.path().filename().string()},
                    {"url_path", "/" + user_id + "/" + entry.path().filename().string()}
                });
            }
        }
        ctx.SendJson(nlohmann::json{{"published", items}}.dump());
    });

    spdlog::info("[Editor] Starting on port {} for user {}", config.server.port, user_id);
    server.Start();
    quarto.StopPreview();
    return 0;
}

} // namespace quarto
