#pragma once
#include <serverweb/Middleware.h>
#include <serverweb/AuthService.h>
#include <memory>
#include <string>
#include <vector>

namespace serverweb::middleware {

class Auth : public IMiddleware {
public:
    Auth(std::shared_ptr<auth::AuthService> auth_service,
         std::vector<std::string> exclude_prefixes = {});

    void Process(RequestContext& ctx, NextFn next) override;

private:
    std::shared_ptr<auth::AuthService> auth_;
    std::vector<std::string> exclude_prefixes_;
    bool IsExcluded(std::string_view path) const;
};

} // namespace serverweb::middleware
