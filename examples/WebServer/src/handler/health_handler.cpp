#include "handler/health_handler.h"

#include <serverweb/HttpResponse.h>

using serverweb::RequestContext;

void HealthHandler::Register(serverweb::WebServer& server) {
    auto mode = mode_;
    server.Get("/api/health", [mode](RequestContext& ctx) {
        ctx.response.Json("{\"status\":\"ok\",\"mode\":\"" + mode + "\"}").Send();
    });
}
