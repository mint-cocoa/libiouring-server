#pragma once

#include <serverweb/Middleware.h>
#include <serverweb/UpstreamPool.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace serverweb {

class HttpRequest;

namespace middleware {

struct ProxyRoute {
    std::string path_prefix;     // "/api/", "/auth/"
    std::string upstream_host;   // "gateway"
    std::uint16_t upstream_port; // 8000
};

struct ReverseProxyOptions {
    std::vector<ProxyRoute> routes;
    std::size_t max_idle_connections = 4;
    std::chrono::seconds connect_timeout = std::chrono::seconds(30);
    std::chrono::seconds response_timeout = std::chrono::seconds(60);
};

class ReverseProxy : public IMiddleware {
public:
    explicit ReverseProxy(ReverseProxyOptions opts);
    void Process(RequestContext& ctx, NextFn next) override;

private:
    const ProxyRoute* FindRoute(std::string_view path) const;
    std::string SerializeRequest(const HttpRequest& request, const ProxyRoute& route) const;

    ReverseProxyOptions opts_;
    std::shared_ptr<UpstreamPool> pool_;
};

} // namespace middleware
} // namespace serverweb
