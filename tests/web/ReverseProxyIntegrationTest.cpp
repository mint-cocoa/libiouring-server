#include <gtest/gtest.h>
#include <serverweb/WebServer.h>
#include <serverweb/ReverseProxy.h>

#include <thread>
#include <chrono>

using namespace serverweb;
using namespace serverweb::middleware;

// Integration tests require io_uring (Linux kernel 5.19+)
// Prefixed with DISABLED_ to skip in CI, run manually with:
//   ./bin/test_reverse_proxy_integration --gtest_also_run_disabled_tests

TEST(ReverseProxyIntegration, DISABLED_ProxyForwardsGetRequest) {
    // 1. Start upstream server (port 19090)
    WebServerConfig upstream_config;
    upstream_config.port = 19090;
    upstream_config.worker_count = 1;
    WebServer upstream(upstream_config);
    upstream.Get("/documents", [](RequestContext& ctx) {
        ctx.response.Json(R"({"documents":[]})").Send();
    });

    // 2. Start proxy server (port 19091)
    WebServerConfig proxy_config;
    proxy_config.port = 19091;
    proxy_config.worker_count = 1;
    WebServer proxy(proxy_config);

    ReverseProxyOptions opts;
    opts.routes = {{"*", "/api/", "127.0.0.1", 19090}};
    proxy.Use(std::make_shared<ReverseProxy>(opts));

    // 3. Start servers in threads
    std::thread upstream_thread([&] { upstream.Start(); });
    std::thread proxy_thread([&] { proxy.Start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 4. Test with curl
    // Manual test: curl http://localhost:19091/api/documents
    // Expected: {"documents":[]}

    // 5. Cleanup
    upstream.Stop();
    proxy.Stop();
    upstream_thread.join();
    proxy_thread.join();
}

TEST(ReverseProxyIntegration, DISABLED_ProxyForwardsHostHeader) {
    // Verifies that the Host header is preserved when forwarding to upstream.
    WebServerConfig upstream_config;
    upstream_config.port = 19092;
    upstream_config.worker_count = 1;
    WebServer upstream(upstream_config);
    upstream.Get("/health", [](RequestContext& ctx) {
        ctx.response.Json(R"({"status":"ok"})").Send();
    });

    WebServerConfig proxy_config;
    proxy_config.port = 19093;
    proxy_config.worker_count = 1;
    WebServer proxy(proxy_config);

    ReverseProxyOptions opts;
    opts.routes = {{"app.mintcocoa.cc", "/", "127.0.0.1", 19092}};
    proxy.Use(std::make_shared<ReverseProxy>(opts));

    std::thread upstream_thread([&] { upstream.Start(); });
    std::thread proxy_thread([&] { proxy.Start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Manual test:
    // curl -s http://localhost:19093/health -H 'Host: app.mintcocoa.cc'
    // Expected: {"status":"ok"}

    upstream.Stop();
    proxy.Stop();
    upstream_thread.join();
    proxy_thread.join();
}

TEST(ReverseProxyIntegration, DISABLED_UnmatchedRoutePassesThrough) {
    // Verifies that requests not matching any proxy route are passed through
    // to local handlers (next middleware / router).
    WebServerConfig proxy_config;
    proxy_config.port = 19094;
    proxy_config.worker_count = 1;
    WebServer proxy(proxy_config);

    ReverseProxyOptions opts;
    opts.routes = {{"app.mintcocoa.cc", "/api/", "127.0.0.1", 19090}};
    proxy.Use(std::make_shared<ReverseProxy>(opts));

    proxy.Get("/local", [](RequestContext& ctx) {
        ctx.response.Json(R"({"local":true})").Send();
    });

    std::thread proxy_thread([&] { proxy.Start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Manual test:
    // curl -s http://localhost:19094/local
    // Expected: {"local":true}
    //
    // curl -s -o /dev/null -w '%{http_code}' http://localhost:19094/unknown
    // Expected: 404

    proxy.Stop();
    proxy_thread.join();
}
