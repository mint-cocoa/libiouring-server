#pragma once

#include <serverweb/Middleware.h>

#include <string>
#include <vector>

namespace serverweb::middleware {

struct CorsOptions {
    std::vector<std::string> allowed_origins = {"*"};
    std::vector<std::string> allowed_methods = {"GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"};
    std::vector<std::string> allowed_headers = {"Content-Type", "Authorization"};
    bool allow_credentials = false;
    std::uint32_t max_age = 86400;
};

class Cors : public IMiddleware {
public:
    explicit Cors(CorsOptions opts = {});
    void Process(RequestContext& ctx, NextFn next) override;

private:
    void AddCorsHeaders(RequestContext& ctx) const;
    std::string JoinStrings(const std::vector<std::string>& v, std::string_view sep) const;

    CorsOptions opts_;
    std::string methods_str_;
    std::string headers_str_;
};

} // namespace serverweb::middleware
