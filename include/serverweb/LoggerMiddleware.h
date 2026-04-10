#pragma once

#include <serverweb/HttpMethod.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/Middleware.h>
#include <serverweb/Router.h>

#include <spdlog/spdlog.h>

#include <chrono>

namespace serverweb::middleware {

class Logger : public IMiddleware {
public:
    void Process(RequestContext& ctx, NextFn next) override {
        auto start = std::chrono::steady_clock::now();
        next();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        spdlog::info("{} {} {} {}us",
            HttpMethodToString(ctx.request.method),
            ctx.request.path,
            static_cast<int>(ctx.response.StatusCode()), us);
    }
};

} // namespace serverweb::middleware
