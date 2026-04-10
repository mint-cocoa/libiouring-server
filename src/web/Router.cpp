#include <serverweb/Router.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpSession.h>

#include <spdlog/spdlog.h>

namespace serverweb {

void Router::Route(HttpMethod method, std::string path, HttpHandler handler) {
    tree_.Insert(method, path, std::move(handler));
}

void Router::Dispatch(RequestContext& ctx) const {
    auto result = tree_.Match(ctx.request.method, ctx.request.path);

    if (result.handler) {
        ctx.request.SetParams(std::move(result.params));
        try {
            (*result.handler)(ctx);
        } catch (const std::exception& e) {
            spdlog::error("Router: handler exception for {} {}: {}",
                          HttpMethodToString(ctx.request.method),
                          ctx.request.path, e.what());
            if (!ctx.response.IsSent()) {
                ctx.response
                    .Status(HttpStatus::kInternalServerError)
                    .ContentType("text/plain")
                    .Body("Internal Server Error")
                    .Send();
            }
        } catch (...) {
            spdlog::error("Router: unknown exception for {} {}",
                          HttpMethodToString(ctx.request.method),
                          ctx.request.path);
            if (!ctx.response.IsSent()) {
                ctx.response
                    .Status(HttpStatus::kInternalServerError)
                    .ContentType("text/plain")
                    .Body("Internal Server Error")
                    .Send();
            }
        }
        ctx.request.ClearParams();
        return;
    }

    // Path exists but method wrong -> 405
    if (result.path_exists) {
        ctx.response
            .Status(HttpStatus::kMethodNotAllowed)
            .ContentType("text/plain")
            .Body("Method Not Allowed")
            .Send();
        return;
    }

    // Unknown path -> 404
    ctx.response
        .Status(HttpStatus::kNotFound)
        .ContentType("text/plain")
        .Body("Not Found")
        .Send();
}

void RequestContext::SendJson(std::string body, HttpStatus status) const {
    auto buf = HttpResponse()
        .Status(status)
        .Json(std::move(body))
        .KeepAlive(response.GetKeepAlive())
        .Build(pool);
    if (buf) session.SendResponse(std::move(buf));
}

void RequestContext::SendError(HttpStatus status, std::string message) const {
    auto buf = HttpResponse()
        .Status(status)
        .Json("{\"error\":\"" + std::move(message) + "\"}")
        .KeepAlive(false)
        .Build(pool);
    if (buf) session.SendResponse(std::move(buf));
}

} // namespace serverweb
