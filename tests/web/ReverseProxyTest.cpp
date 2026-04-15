#include <gtest/gtest.h>
#include <serverweb/ReverseProxy.h>

using namespace serverweb;
using namespace serverweb::middleware;

TEST(ReverseProxyTest, RouteConfiguration) {
    ReverseProxyOptions opts;
    opts.routes = {
        {"app.mintcocoa.cc", "/api/", "gateway", 8000},
        {"app.mintcocoa.cc", "/auth/", "gateway", 8000},
    };
    ReverseProxy proxy(opts);

    EXPECT_EQ(opts.routes[0].host_pattern, "app.mintcocoa.cc");
    EXPECT_EQ(opts.routes[0].path_prefix, "/api/");
    EXPECT_EQ(opts.routes[0].upstream_host, "gateway");
    EXPECT_EQ(opts.routes[0].upstream_port, 8000);
}

TEST(ProxyRouteTest, UpstreamTargetFromRoute) {
    ProxyRoute route{"*", "/api/", "gateway", 8000};
    UpstreamTarget target{route.upstream_host, route.upstream_port};
    EXPECT_EQ(target.Key(), "gateway:8000");
}
