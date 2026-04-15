#include <serverweb/ReverseProxy.h>
#include <serverweb/HttpMethod.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpSession.h>
#include <serverweb/HttpStatus.h>
#include <serverweb/Router.h>

#include <servercore/ring/IoRing.h>

#include <memory>
#include <string>

namespace serverweb::middleware {

ReverseProxy::ReverseProxy(ReverseProxyOptions opts)
    : opts_(std::move(opts)),
      pool_(std::make_shared<UpstreamPool>(opts_.max_idle_connections,
                                           opts_.connect_timeout)) {}

void ReverseProxy::Process(RequestContext& ctx, NextFn next) {
    const auto* route = FindRoute(ctx.request.path);
    if (!route) {
        next();
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

const ProxyRoute* ReverseProxy::FindRoute(std::string_view path) const {
    for (const auto& route : opts_.routes) {
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

} // namespace serverweb::middleware
