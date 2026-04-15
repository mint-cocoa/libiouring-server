#include <serverweb/ReverseProxy.h>
#include <serverweb/HttpMethod.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpSession.h>
#include <serverweb/HttpStatus.h>
#include <serverweb/Router.h>

#include <servercore/ring/IoRing.h>

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace serverweb::middleware {

ReverseProxy::ReverseProxy(ReverseProxyOptions opts)
    : opts_(std::move(opts)),
      pool_(std::make_shared<UpstreamPool>(opts_.max_idle_connections,
                                           opts_.connect_timeout)) {}

void ReverseProxy::Process(RequestContext& ctx, NextFn next) {
    // Extract hostname from Host header (strip port if present)
    auto host_header = ctx.request.GetHeader("Host");
    auto colon = host_header.find(':');
    auto host = host_header.substr(0, colon);

    const auto* route = FindRoute(host, ctx.request.path);
    if (!route) {
        next();
        return;
    }

    // WebSocket upgrade detection
    auto upgrade = ctx.request.GetHeader("Upgrade");
    if (upgrade == "websocket") {
        HandleWebSocketProxy(ctx, *route);
        return;
    }

    ctx.Defer();

    auto* ring = servercore::ring::IoRing::Current();
    if (!ring) {
        ctx.SendError(HttpStatus::kInternalServerError, "No IoRing available");
        return;
    }

    UpstreamTarget target{route->upstream_host, route->upstream_port};
    std::string raw_request = SerializeRequest(ctx.request, *route);

    // Capture session as shared_ptr and pool as raw pointer.
    // RequestContext is stack-local and will be destroyed after Process() returns,
    // so we must not capture ctx by reference.
    auto session_ptr = std::static_pointer_cast<HttpSession>(ctx.session.shared_from_this());
    auto* pool_ptr = &ctx.pool;

    pool_->Forward(
        *ring, ctx.pool, target, std::move(raw_request),
        [session_ptr, pool_ptr](
            int status_code,
            std::vector<std::pair<std::string, std::string>> headers,
            std::vector<std::byte> body) {
            HttpResponse response(*session_ptr, *pool_ptr);
            response.Status(static_cast<HttpStatus>(status_code));
            for (auto& [name, value] : headers) {
                response.Header(std::move(name), std::move(value));
            }
            response.Body(std::string(reinterpret_cast<const char*>(body.data()), body.size()));
            response.Send();
        },
        [session_ptr, pool_ptr](std::string error) {
            HttpResponse response(*session_ptr, *pool_ptr);
            response.Status(HttpStatus::kBadGateway)
                .Body("Bad Gateway: " + error)
                .Send();
        });
}

const ProxyRoute* ReverseProxy::FindRoute(std::string_view host, std::string_view path) const {
    for (const auto& route : opts_.routes) {
        // Host matching: "*" matches any host
        if (route.host_pattern != "*" && route.host_pattern != host) {
            continue;
        }
        // Path prefix matching
        if (path.starts_with(route.path_prefix)) {
            return &route;
        }
    }
    return nullptr;
}

std::string ReverseProxy::SerializeRequest(const HttpRequest& request,
                                           const ProxyRoute& route) const {
    std::string result;
    result.reserve(512);

    // Request line
    result.append(HttpMethodToString(request.method));
    result.append(" ");
    result.append(request.path);
    if (!request.query.empty()) {
        result.append("?");
        result.append(request.query);
    }
    result.append(" HTTP/1.1\r\n");

    // Host header for upstream
    result.append("Host: ");
    result.append(route.upstream_host);
    result.append(":");
    result.append(std::to_string(route.upstream_port));
    result.append("\r\n");

    // Forward original headers except Host
    for (const auto& header : request.headers()) {
        // Skip Host — we set our own above
        if (header.name.size() == 4) {
            bool is_host = (header.name[0] == 'H' || header.name[0] == 'h') &&
                           (header.name[1] == 'o' || header.name[1] == 'O') &&
                           (header.name[2] == 's' || header.name[2] == 'S') &&
                           (header.name[3] == 't' || header.name[3] == 'T');
            if (is_host) continue;
        }
        result.append(header.name);
        result.append(": ");
        result.append(header.value);
        result.append("\r\n");
    }

    // Add proxy headers
    result.append("X-Forwarded-For: client\r\n");
    result.append("Connection: close\r\n");

    // End of headers
    result.append("\r\n");

    // Body
    if (!request.body.empty()) {
        result.append(request.body);
    }

    return result;
}

void ReverseProxy::HandleWebSocketProxy(RequestContext& ctx, const ProxyRoute& route) {
    auto* ring = servercore::ring::IoRing::Current();
    if (!ring) {
        ctx.SendError(HttpStatus::kInternalServerError, "No IoRing available");
        return;
    }

    UpstreamTarget target{route.upstream_host, route.upstream_port};
    auto handler = std::make_shared<WsProxyHandler>(*ring, ctx.pool, target, ctx.request);
    ctx.session.UpgradeToWebSocket(handler);
}

// ---------------------------------------------------------------------------
// WsProxyHandler
// ---------------------------------------------------------------------------

WsProxyHandler::WsProxyHandler(servercore::ring::IoRing& ring,
                                servercore::buffer::BufferPool& pool,
                                const UpstreamTarget& target,
                                const HttpRequest& original_request)
    : ring_(ring), pool_(pool), target_(target) {
    // Build the upgrade request to forward to upstream
    upgrade_request_.reserve(512);

    // Request line
    upgrade_request_.append("GET ");
    upgrade_request_.append(original_request.path);
    if (!original_request.query.empty()) {
        upgrade_request_.append("?");
        upgrade_request_.append(original_request.query);
    }
    upgrade_request_.append(" HTTP/1.1\r\n");

    // Host header pointing to upstream
    upgrade_request_.append("Host: ");
    upgrade_request_.append(target_.host);
    upgrade_request_.append(":");
    upgrade_request_.append(std::to_string(target_.port));
    upgrade_request_.append("\r\n");

    // Forward WebSocket-specific headers
    static constexpr std::string_view kWsHeaders[] = {
        "Upgrade",
        "Connection",
        "Sec-WebSocket-Key",
        "Sec-WebSocket-Version",
        "Sec-WebSocket-Protocol",
        "Sec-WebSocket-Extensions",
        "Origin",
    };
    for (const auto& name : kWsHeaders) {
        auto val = original_request.GetHeader(name);
        if (!val.empty()) {
            upgrade_request_.append(name);
            upgrade_request_.append(": ");
            upgrade_request_.append(val);
            upgrade_request_.append("\r\n");
        }
    }

    upgrade_request_.append("\r\n");
}

void WsProxyHandler::OnOpen(HttpSession& client_session) {
    spdlog::debug("[WsProxy] Client connected, connecting to upstream {}:{}",
                  target_.host, target_.port);

    upstream_ = std::make_shared<UpstreamSession>(ring_, pool_);
    upstream_->Init();

    // Capture client_session as shared_ptr so it stays alive in callbacks
    auto session_ptr = std::static_pointer_cast<HttpSession>(
        client_session.shared_from_this());

    upstream_->Connect(
        target_.host, target_.port, std::move(upgrade_request_),
        // on_response: upstream sent back a 101 (or other); we already sent
        // 101 to the client via UpgradeToWebSocket — nothing more to do here.
        [](int status_code,
           std::vector<std::pair<std::string, std::string>> /*headers*/,
           std::vector<std::byte> /*body*/) {
            spdlog::debug("[WsProxy] Upstream handshake response status {}", status_code);
        },
        // on_error: close the client WebSocket
        [session_ptr](std::string error) {
            spdlog::warn("[WsProxy] Upstream connect error: {}", error);
            session_ptr->WsClose(1014, "upstream error");
        });
}

void WsProxyHandler::OnMessage(HttpSession& /*client_session*/,
                                std::string_view data, bool is_text) {
    // Full bidirectional relay requires upstream WebSocket framing support
    // (to be added later). For now just log.
    spdlog::debug("[WsProxy] Client→Upstream: {} bytes ({})",
                  data.size(), is_text ? "text" : "binary");
}

void WsProxyHandler::OnClose(HttpSession& /*client_session*/,
                              uint16_t code, std::string_view reason) {
    spdlog::debug("[WsProxy] Client closed: code={} reason={}", code, reason);
    if (upstream_) {
        upstream_->Close();
        upstream_.reset();
    }
}

} // namespace serverweb::middleware
