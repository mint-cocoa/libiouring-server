#pragma once

#include <serverweb/HttpMethod.h>
#include <serverweb/HttpRequest.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpSession.h>
#include <serverweb/HttpStatus.h>
#include <serverweb/RadixTree.h>

#include <servercore/buffer/SendBuffer.h>

#include <functional>
#include <memory>
#include <string>

namespace serverweb {

// Context passed to route handlers.
//
// For synchronous handlers, use response.Send() directly:
//   ctx.response.Json(R"({"ok":true})").Send();
//
// For async handlers (e.g. DB queries), call Defer() to prevent the
// automatic empty-response send, then use SendJson/SendError from
// the async callback:
//   ctx.Defer();
//   db->Execute("SELECT ...", [ctx](PgResult r) {
//       ctx.SendJson(R"({"rows":...})");
//   });
struct RequestContext {
    HttpSession& session;
    HttpRequest& request;
    HttpResponse& response;
    servercore::buffer::BufferPool& pool;

    // Suppress the automatic response send at the end of HandleRequest.
    // The handler takes responsibility for sending a response later
    // (e.g. from a DB callback). The session stays alive via self_ref_.
    void Defer() { response.MarkSent(); }

    // Convenience: send a JSON response from an async callback. Safe to
    // call from IoWorker thread (which is where DB callbacks arrive).
    // Defined in Router.cpp to avoid incomplete-type issues.
    void SendJson(std::string body, HttpStatus status = HttpStatus::kOk) const;
    void SendError(HttpStatus status, std::string message) const;
};

using HttpHandler = std::function<void(RequestContext&)>;

// Radix tree-based HTTP router.
// Supports static paths, :param, *wildcard.
// Distinguishes 404 (unknown path) from 405 (known path, wrong method).
class Router {
public:
    void Route(HttpMethod method, std::string path, HttpHandler handler);

    // Dispatch a request. Sends 404/405/500 automatically on error.
    void Dispatch(RequestContext& ctx) const;

private:
    RadixTree tree_;
};

} // namespace serverweb
