#include <serverweb/CorsMiddleware.h>
#include <serverweb/HttpMethod.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpStatus.h>
#include <serverweb/Router.h>

namespace serverweb::middleware {

Cors::Cors(CorsOptions opts)
    : opts_(std::move(opts)) {
    methods_str_ = JoinStrings(opts_.allowed_methods, ", ");
    headers_str_ = JoinStrings(opts_.allowed_headers, ", ");
}

void Cors::Process(RequestContext& ctx, NextFn next) {
    AddCorsHeaders(ctx);

    // Handle preflight OPTIONS request
    if (ctx.request.method == HttpMethod::kOptions) {
        ctx.response
            .Status(HttpStatus::kNoContent)
            .Header("Access-Control-Max-Age", std::to_string(opts_.max_age))
            .Send();
        return;  // Don't call next() -- short-circuit
    }

    next();
}

void Cors::AddCorsHeaders(RequestContext& ctx) const {
    // Origin
    if (opts_.allowed_origins.size() == 1 && opts_.allowed_origins[0] == "*") {
        ctx.response.Header("Access-Control-Allow-Origin", "*");
    } else {
        auto origin = ctx.request.GetHeader("Origin");
        for (auto& allowed : opts_.allowed_origins) {
            if (allowed == std::string(origin)) {
                ctx.response.Header("Access-Control-Allow-Origin", std::string(origin));
                ctx.response.Header("Vary", "Origin");
                break;
            }
        }
    }

    // Methods & Headers
    ctx.response.Header("Access-Control-Allow-Methods", methods_str_);
    ctx.response.Header("Access-Control-Allow-Headers", headers_str_);

    if (opts_.allow_credentials) {
        ctx.response.Header("Access-Control-Allow-Credentials", "true");
    }
}

std::string Cors::JoinStrings(const std::vector<std::string>& v, std::string_view sep) const {
    std::string result;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) result += sep;
        result += v[i];
    }
    return result;
}

} // namespace serverweb::middleware
