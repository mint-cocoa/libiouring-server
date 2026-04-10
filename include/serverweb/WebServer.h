#pragma once

#include <serverweb/HttpMethod.h>
#include <serverweb/RouteGroup.h>
#include <serverweb/Router.h>
#include <serverweb/WebSocketHandler.h>

#include <servercore/ring/IoRing.h>
#include <servercore/io/Listener.h>
#include <servercore/buffer/SendBuffer.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace serverweb {

struct WebServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::uint16_t worker_count = 4;
    std::uint32_t ring_queue_depth = 2048;
    std::uint32_t ring_buf_count = 4096;
    std::uint32_t ring_buf_size = 4096;
    std::chrono::milliseconds io_timeout{1};
};

// High-level HTTP web server built on ServerCore's IoRing.
//
// Each worker thread owns an independent io_uring instance and listener
// (SO_REUSEPORT). This mirrors the Ring-per-thread architecture used by
// GameServer — the only difference is that incoming connections are
// upgraded to HttpSession instead of PacketSession.
//
// Usage:
//   WebServer server(config);
//   server.Get("/", [](RequestContext& ctx) {
//       ctx.response.Body("Hello").Send();
//   });
//   server.Start();   // blocks until Stop()
//   server.Stop();
class WebServer {
public:
    explicit WebServer(const WebServerConfig& config);
    ~WebServer();

    // Global middleware (must be called before route registration)
    void Use(MiddlewareRef mw);

    // Route registration (must be called before Start)
    void Route(HttpMethod method, std::string path, HttpHandler handler);

    void Get(std::string path, HttpHandler handler) {
        Route(HttpMethod::kGet, std::move(path), std::move(handler));
    }
    void Post(std::string path, HttpHandler handler) {
        Route(HttpMethod::kPost, std::move(path), std::move(handler));
    }
    void Put(std::string path, HttpHandler handler) {
        Route(HttpMethod::kPut, std::move(path), std::move(handler));
    }
    void Delete(std::string path, HttpHandler handler) {
        Route(HttpMethod::kDelete, std::move(path), std::move(handler));
    }

    // WebSocket endpoint registration
    void WebSocket(std::string path,
                   std::shared_ptr<ws::WebSocketHandler> handler);

    RouteGroup Group(std::string prefix);

    void Start();
    void Stop();

private:
    struct Worker {
        std::unique_ptr<servercore::ring::IoRing> ring;
        std::shared_ptr<servercore::io::Listener> listener;
        servercore::buffer::BufferPool pool;
        std::thread thread;
    };

    void WorkerLoop(Worker& w);

    WebServerConfig config_;
    Router router_;
    std::vector<MiddlewareRef> global_middlewares_;
    std::vector<std::unique_ptr<Worker>> workers_;
    bool running_ = false;
};

} // namespace serverweb
