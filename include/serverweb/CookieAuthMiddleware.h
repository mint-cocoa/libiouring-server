#pragma once

#include <serverweb/Middleware.h>
#include <serverweb/JwtCodec.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace serverweb::middleware {

struct CookieAuthOptions {
    std::string jwt_secret;
    std::string cookie_name = "token";
    std::vector<std::string> exclude_paths = {};
};

class CookieAuth : public IMiddleware {
public:
    explicit CookieAuth(CookieAuthOptions opts);
    void Process(RequestContext& ctx, NextFn next) override;

    static std::optional<std::string> ExtractCookie(
        std::string_view cookie_header, std::string_view name);

private:
    bool IsExcluded(std::string_view path) const;

    CookieAuthOptions opts_;
    auth::JwtCodec jwt_;
};

} // namespace serverweb::middleware
