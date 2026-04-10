#pragma once

#include <serverweb/HttpMethod.h>
#include <serverweb/Middleware.h>
#include <serverweb/Router.h>

#include <memory>
#include <string>
#include <vector>

namespace serverweb {

class RouteGroup {
public:
    RouteGroup(Router& router, std::string prefix,
               std::vector<MiddlewareRef> global_middlewares = {});

    void Get(std::string path, HttpHandler handler);
    void Post(std::string path, HttpHandler handler);
    void Put(std::string path, HttpHandler handler);
    void Delete(std::string path, HttpHandler handler);
    void Patch(std::string path, HttpHandler handler);

    RouteGroup Group(std::string sub_prefix);

    // Group-level middleware (must be called before route registration in this group)
    void Use(MiddlewareRef mw);

private:
    void AddRoute(HttpMethod method, std::string path, HttpHandler handler);
    HttpHandler WrapWithMiddleware(HttpHandler handler) const;

    Router& router_;
    std::string prefix_;
    std::vector<MiddlewareRef> global_middlewares_;
    std::vector<MiddlewareRef> group_middlewares_;
};

} // namespace serverweb
