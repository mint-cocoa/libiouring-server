#include <serverweb/CookieAuthMiddleware.h>
#include <serverweb/Router.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>

namespace serverweb::middleware {

CookieAuth::CookieAuth(CookieAuthOptions opts)
    : opts_(std::move(opts))
    , jwt_(opts_.jwt_secret) {}

void CookieAuth::Process(RequestContext& ctx, NextFn next) {
    if (IsExcluded(ctx.request.path)) {
        next();
        return;
    }

    auto cookie_header = ctx.request.GetHeader("Cookie");
    if (cookie_header.empty()) {
        ctx.response.Status(HttpStatus::kUnauthorized)
            .Json(R"({"error":"not authenticated"})").Send();
        return;
    }

    auto token = ExtractCookie(cookie_header, opts_.cookie_name);
    if (!token.has_value()) {
        ctx.response.Status(HttpStatus::kUnauthorized)
            .Json(R"({"error":"token cookie not found"})").Send();
        return;
    }

    auto claims = jwt_.Decode(token.value());
    if (!claims.has_value()) {
        ctx.response.Status(HttpStatus::kUnauthorized)
            .Json(R"({"error":"invalid or expired token"})").Send();
        return;
    }

    ctx.request.authenticated_user_id = claims.value().sub;
    next();
}

std::optional<std::string> CookieAuth::ExtractCookie(
    std::string_view cookie_header, std::string_view name) {
    if (cookie_header.empty()) return std::nullopt;

    std::string_view remaining = cookie_header;
    while (!remaining.empty()) {
        while (!remaining.empty() && remaining.front() == ' ') {
            remaining.remove_prefix(1);
        }

        auto semi = remaining.find(';');
        auto pair = remaining.substr(0, semi);

        auto eq = pair.find('=');
        if (eq != std::string_view::npos) {
            auto key = pair.substr(0, eq);
            while (!key.empty() && key.back() == ' ') {
                key.remove_suffix(1);
            }
            if (key == name) {
                return std::string(pair.substr(eq + 1));
            }
        }

        if (semi == std::string_view::npos) break;
        remaining.remove_prefix(semi + 1);
    }

    return std::nullopt;
}

bool CookieAuth::IsExcluded(std::string_view path) const {
    for (const auto& prefix : opts_.exclude_paths) {
        if (path.substr(0, prefix.size()) == prefix) {
            return true;
        }
    }
    return false;
}

} // namespace serverweb::middleware
