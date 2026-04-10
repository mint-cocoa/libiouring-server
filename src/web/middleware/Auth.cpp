#include <serverweb/AuthMiddleware.h>
#include <serverweb/Router.h>
#include <serverweb/HttpResponse.h>
#include <nlohmann/json.hpp>

namespace serverweb::middleware {

Auth::Auth(std::shared_ptr<auth::AuthService> auth_service,
           std::vector<std::string> exclude_prefixes)
    : auth_(std::move(auth_service))
    , exclude_prefixes_(std::move(exclude_prefixes)) {}

bool Auth::IsExcluded(std::string_view path) const {
    for (auto& prefix : exclude_prefixes_) {
        if (path.substr(0, prefix.size()) == prefix) return true;
    }
    return false;
}

void Auth::Process(RequestContext& ctx, NextFn next) {
    if (IsExcluded(ctx.request.path)) {
        next();
        return;
    }

    auto auth_header = ctx.request.GetHeader("Authorization");
    if (auth_header.empty()) {
        ctx.response.Status(HttpStatus::kUnauthorized)
            .Json(R"({"error":"missing authorization header"})").Send();
        return;
    }

    constexpr std::string_view bearer = "Bearer ";
    if (auth_header.size() <= bearer.size() ||
        auth_header.substr(0, bearer.size()) != bearer) {
        ctx.response.Status(HttpStatus::kUnauthorized)
            .Json(R"({"error":"invalid authorization format"})").Send();
        return;
    }

    auto token = auth_header.substr(bearer.size());
    auto result = auth_->VerifyToken(token);

    if (!result.has_value()) {
        ctx.response.Status(HttpStatus::kUnauthorized)
            .Json(R"({"error":"invalid or expired token"})").Send();
        return;
    }

    ctx.request.authenticated_user_id = result.value().sub;
    next();
}

} // namespace serverweb::middleware
