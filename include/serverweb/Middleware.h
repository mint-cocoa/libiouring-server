#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace serverweb {

struct RequestContext;

using NextFn = std::function<void()>;

class IMiddleware {
public:
    virtual ~IMiddleware() = default;
    virtual void Process(RequestContext& ctx, NextFn next) = 0;
};

using MiddlewareRef = std::shared_ptr<IMiddleware>;

class Pipeline {
public:
    void Use(MiddlewareRef mw);
    void Execute(RequestContext& ctx, std::function<void()> final_handler) const;

    bool Empty() const { return middlewares_.empty(); }

private:
    std::vector<MiddlewareRef> middlewares_;
};

} // namespace serverweb
