#include <serverweb/WebServer.h>
#include <serverweb/HttpSession.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/HttpStatus.h>
#include <serverweb/WsHandshake.h>

#include <spdlog/spdlog.h>

namespace serverweb {

WebServer::WebServer(const WebServerConfig& config)
    : config_(config) {}

WebServer::~WebServer() {
    Stop();
}

void WebServer::Use(MiddlewareRef mw) {
    global_middlewares_.push_back(std::move(mw));
}

void WebServer::Route(HttpMethod method, std::string path, HttpHandler handler) {
    if (!global_middlewares_.empty()) {
        auto pipeline = std::make_shared<Pipeline>();
        for (auto& mw : global_middlewares_) pipeline->Use(mw);

        auto wrapped = [pipeline = std::move(pipeline),
                        handler = std::move(handler)](RequestContext& ctx) {
            pipeline->Execute(ctx, [&ctx, &handler]() { handler(ctx); });
        };
        router_.Route(method, std::move(path), std::move(wrapped));
    } else {
        router_.Route(method, std::move(path), std::move(handler));
    }
}

void WebServer::WebSocket(std::string path,
                           std::shared_ptr<ws::WebSocketHandler> handler) {
    Route(HttpMethod::kGet, std::move(path),
        [handler](RequestContext& ctx) {
            if (!ws::WsHandshake::ValidateUpgrade(ctx.request)) {
                ctx.response.Status(HttpStatus::kBadRequest)
                    .Body("WebSocket upgrade required").Send();
                return;
            }

            auto key = ctx.request.GetHeader("Sec-WebSocket-Key");
            auto accept = ws::WsHandshake::ComputeAcceptKey(key);
            auto buf = ws::WsHandshake::BuildUpgradeResponse(accept, ctx.pool);

            ctx.session.SendResponse(std::move(buf));
            ctx.response.MarkSent();
            ctx.session.UpgradeToWebSocket(handler);
        });
}

RouteGroup WebServer::Group(std::string prefix) {
    return RouteGroup(router_, std::move(prefix), global_middlewares_);
}

void WebServer::Start() {
    running_ = true;
    const Router* router = &router_;

    for (std::uint16_t i = 0; i < config_.worker_count; ++i) {
        auto w = std::make_unique<Worker>();

        servercore::ring::IoRingConfig ring_cfg;
        ring_cfg.queue_depth = config_.ring_queue_depth;
        ring_cfg.buf_ring.buf_count = config_.ring_buf_count;
        ring_cfg.buf_ring.buf_size = config_.ring_buf_size;
        ring_cfg.buf_ring.group_id = static_cast<std::uint16_t>(i + 1);

        auto ring_result = servercore::ring::IoRing::Create(ring_cfg);
        if (!ring_result) {
            spdlog::error("WebServer: failed to create IoRing for worker {}", i);
            continue;
        }
        w->ring = std::move(*ring_result);

        servercore::io::SessionFactory factory =
            [router](int fd, servercore::ring::IoRing& ring,
                     servercore::buffer::BufferPool& pool,
                     servercore::ContextId)
                -> servercore::io::SessionRef {
            return std::make_shared<HttpSession>(fd, ring, pool, *router);
        };

        servercore::Address addr{config_.host, config_.port};
        w->listener = std::make_shared<servercore::io::Listener>(
            *w->ring, w->pool, addr, std::move(factory), i, 0);

        auto listen_result = w->listener->Start();
        if (!listen_result) {
            spdlog::error("WebServer: worker {} failed to listen on port {}",
                          i, config_.port);
            continue;
        }

        w->thread = std::thread([this, raw = w.get()] { WorkerLoop(*raw); });
        workers_.push_back(std::move(w));
    }

    spdlog::info("WebServer: listening on {}:{} ({} workers)",
                 config_.host, config_.port, config_.worker_count);
}

void WebServer::Stop() {
    running_ = false;
    for (auto& w : workers_) {
        if (w->thread.joinable()) w->thread.join();
    }
    workers_.clear();
    spdlog::info("WebServer: stopped");
}

void WebServer::WorkerLoop(Worker& w) {
    servercore::ring::IoRing::SetCurrent(w.ring.get());
    while (running_) {
        w.ring->Dispatch(config_.io_timeout);
        w.ring->ProcessPostedTasks();
    }
}

} // namespace serverweb
