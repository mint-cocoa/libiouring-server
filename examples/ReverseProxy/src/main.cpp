#include <serverweb/WebServer.h>
#include <serverweb/ReverseProxy.h>
#include <serverweb/StaticFiles.h>
#include <serverweb/LoggerMiddleware.h>

#include <spdlog/spdlog.h>

int main() {
    serverweb::WebServerConfig config;
    config.port = 8080;
    config.worker_count = 2;

    serverweb::WebServer server(config);

    // Middleware: logging
    server.Use(std::make_shared<serverweb::middleware::Logger>());

    // Middleware: reverse proxy (app domain → Gateway)
    serverweb::middleware::ReverseProxyOptions proxy_opts;
    proxy_opts.routes = {
        // app.mintcocoa.cc — API/Auth/Preview → Gateway
        {"app.mintcocoa.cc", "/api/",     "gateway", 8000},
        {"app.mintcocoa.cc", "/auth/",    "gateway", 8000},
        {"app.mintcocoa.cc", "/preview/", "gateway", 8000},
    };
    server.Use(std::make_shared<serverweb::middleware::ReverseProxy>(proxy_opts));

    // Middleware: static files (app SPA)
    serverweb::middleware::StaticFilesOptions app_static;
    app_static.root = "/srv/frontend";
    app_static.prefix = "/";
    app_static.index_files = {"index.html"};
    server.Use(std::make_shared<serverweb::middleware::StaticFiles>(app_static));

    // Health check endpoint
    server.Get("/health", [](serverweb::RequestContext& ctx) {
        ctx.response.Json(R"({"status":"ok"})").Send();
    });

    spdlog::info("Quarto Platform Proxy starting on port {}", config.port);
    server.Start();

    return 0;
}
