#include <serverweb/RouteGroup.h>

namespace serverweb {

RouteGroup::RouteGroup(Router& router, std::string prefix,
                       std::vector<MiddlewareRef> global_middlewares)
    : router_(router)
    , prefix_(std::move(prefix))
    , global_middlewares_(std::move(global_middlewares)) {}

void RouteGroup::Get(std::string path, HttpHandler handler) {
    AddRoute(HttpMethod::kGet, std::move(path), std::move(handler));
}

void RouteGroup::Post(std::string path, HttpHandler handler) {
    AddRoute(HttpMethod::kPost, std::move(path), std::move(handler));
}

void RouteGroup::Put(std::string path, HttpHandler handler) {
    AddRoute(HttpMethod::kPut, std::move(path), std::move(handler));
}

void RouteGroup::Delete(std::string path, HttpHandler handler) {
    AddRoute(HttpMethod::kDelete, std::move(path), std::move(handler));
}

void RouteGroup::Patch(std::string path, HttpHandler handler) {
    AddRoute(HttpMethod::kPatch, std::move(path), std::move(handler));
}

RouteGroup RouteGroup::Group(std::string sub_prefix) {
    // Nested group inherits global + this group's middlewares
    std::vector<MiddlewareRef> combined;
    combined.reserve(global_middlewares_.size() + group_middlewares_.size());
    combined.insert(combined.end(), global_middlewares_.begin(), global_middlewares_.end());
    combined.insert(combined.end(), group_middlewares_.begin(), group_middlewares_.end());
    return RouteGroup(router_, prefix_ + sub_prefix, std::move(combined));
}

void RouteGroup::Use(MiddlewareRef mw) {
    group_middlewares_.push_back(std::move(mw));
}

void RouteGroup::AddRoute(HttpMethod method, std::string path, HttpHandler handler) {
    router_.Route(method, prefix_ + path, WrapWithMiddleware(std::move(handler)));
}

HttpHandler RouteGroup::WrapWithMiddleware(HttpHandler handler) const {
    if (global_middlewares_.empty() && group_middlewares_.empty()) {
        return handler;
    }

    // Build pipeline: global middlewares first, then group middlewares
    auto pipeline = std::make_shared<Pipeline>();
    for (auto& mw : global_middlewares_) pipeline->Use(mw);
    for (auto& mw : group_middlewares_) pipeline->Use(mw);

    return [pipeline = std::move(pipeline),
            handler = std::move(handler)](RequestContext& ctx) {
        pipeline->Execute(ctx, [&ctx, &handler]() { handler(ctx); });
    };
}

} // namespace serverweb
