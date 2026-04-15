#pragma once

#include <serverweb/Middleware.h>
#include <serverweb/UpstreamPool.h>
#include <serverweb/WebSocketHandler.h>
#include <serverweb/UpstreamSession.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace serverweb {

class HttpRequest;

namespace middleware {

class WsProxyHandler : public ws::WebSocketHandler {
public:
    WsProxyHandler(servercore::ring::IoRing& ring,
                   servercore::buffer::BufferPool& pool,
                   const UpstreamTarget& target,
                   const HttpRequest& original_request);

    void OnOpen(HttpSession& client_session) override;
    void OnMessage(HttpSession& client_session, std::string_view data, bool is_text) override;
    void OnClose(HttpSession& client_session, uint16_t code, std::string_view reason) override;

private:
    servercore::ring::IoRing& ring_;
    servercore::buffer::BufferPool& pool_;
    UpstreamTarget target_;
    std::string upgrade_request_;
    std::shared_ptr<UpstreamSession> upstream_;
};

struct ProxyRoute {
    std::string host_pattern;    // "app.mintcocoa.cc", "blog.mintcocoa.cc", "*" (wildcard = any host)
    std::string path_prefix;     // "/api/", "/auth/", "/"
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
    const ProxyRoute* FindRoute(std::string_view host, std::string_view path) const;
    std::string SerializeRequest(const HttpRequest& request, const ProxyRoute& route) const;
    void HandleWebSocketProxy(RequestContext& ctx, const ProxyRoute& route);

    ReverseProxyOptions opts_;
    std::shared_ptr<UpstreamPool> pool_;
};

} // namespace middleware
} // namespace serverweb
