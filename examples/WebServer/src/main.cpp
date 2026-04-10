// WebServer example — REST API with PostgreSQL
//
// Demonstrates ServerCore's web framework capabilities:
// - Radix-tree HTTP router with route parameters (:id)
// - Middleware pipeline (CORS, logging)
// - Route groups (/api/...)
// - Async DB queries via PgConnectionPool + IoRing::Post callback
// - JSON request/response with nlohmann/json
//
// Usage:
//   ./WebServer                              (in-memory, no DB)
//   WEBSERVER_DB_DSN="host=localhost ..." ./WebServer  (PostgreSQL)

#include <serverweb/WebServer.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/CorsMiddleware.h>
#include <serverweb/LoggerMiddleware.h>

#include <serverstorage/PgConnectionPool.h>
#include <serverstorage/StorageConfig.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <signal.h>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
using serverweb::RequestContext;
using serverweb::HttpStatus;

static std::atomic<bool> g_running{true};
void SignalHandler(int) { g_running = false; }

// ─── In-memory item store (fallback when no DB is configured) ─────────

struct Item {
    int64_t id = 0;
    std::string name;
    int32_t price = 0;
};

class MemoryStore {
public:
    Item Create(const std::string& name, int32_t price) {
        std::lock_guard lk(mu_);
        Item it{next_id_++, name, price};
        items_[it.id] = it;
        return it;
    }

    std::vector<Item> List() const {
        std::lock_guard lk(mu_);
        std::vector<Item> out;
        out.reserve(items_.size());
        for (auto& [_, v] : items_) out.push_back(v);
        return out;
    }

    Item* Get(int64_t id) {
        std::lock_guard lk(mu_);
        auto it = items_.find(id);
        return it != items_.end() ? &it->second : nullptr;
    }

    bool Delete(int64_t id) {
        std::lock_guard lk(mu_);
        return items_.erase(id) > 0;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<int64_t, Item> items_;
    int64_t next_id_ = 1;
};

static json ItemToJson(const Item& it) {
    return {{"id", it.id}, {"name", it.name}, {"price", it.price}};
}

// ─── Schema bootstrap (PostgreSQL) ────────────────────────────────────

static void BootstrapSchema(const serverstorage::StorageConfig& cfg) {
    serverstorage::PgConnection conn(cfg);
    if (!conn.Connect()) {
        spdlog::error("Schema bootstrap: connect failed");
        return;
    }
    conn.Execute(
        "CREATE TABLE IF NOT EXISTS items ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  price INTEGER NOT NULL DEFAULT 0"
        ")", {});
    spdlog::info("Schema: items table ready");
}

// ─── Route handlers ───────────────────────────────────────────────────

// -- In-memory routes (no DB) --

static void RegisterMemoryRoutes(serverweb::WebServer& server,
                                  std::shared_ptr<MemoryStore> store) {
    auto api = server.Group("/api");

    api.Get("/items", [store](RequestContext& ctx) {
        auto items = store->List();
        json arr = json::array();
        for (auto& it : items) arr.push_back(ItemToJson(it));
        ctx.response.Json(arr.dump()).Send();
    });

    api.Get("/items/:id", [store](RequestContext& ctx) {
        auto id_str = ctx.request.Param("id");
        int64_t id = std::atoll(std::string(id_str).c_str());
        auto* item = store->Get(id);
        if (!item) {
            ctx.response.Status(HttpStatus::kNotFound)
                .Json(R"({"error":"not found"})").Send();
            return;
        }
        ctx.response.Json(ItemToJson(*item).dump()).Send();
    });

    api.Post("/items", [store](RequestContext& ctx) {
        auto body = json::parse(ctx.request.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name")) {
            ctx.response.Status(HttpStatus::kBadRequest)
                .Json(R"({"error":"name required"})").Send();
            return;
        }
        std::string name = body["name"];
        int32_t price = body.value("price", 0);
        auto item = store->Create(name, price);
        ctx.response.Status(HttpStatus::kCreated)
            .Json(ItemToJson(item).dump()).Send();
    });

    api.Delete("/items/:id", [store](RequestContext& ctx) {
        auto id_str = ctx.request.Param("id");
        int64_t id = std::atoll(std::string(id_str).c_str());
        if (!store->Delete(id)) {
            ctx.response.Status(HttpStatus::kNotFound)
                .Json(R"({"error":"not found"})").Send();
            return;
        }
        ctx.response.Json(R"({"deleted":true})").Send();
    });
}

// -- PostgreSQL routes (async DB queries) --

static void RegisterDbRoutes(serverweb::WebServer& server,
                              std::shared_ptr<serverstorage::PgConnectionPool> pool) {
    auto api = server.Group("/api");

    // GET /api/items — list all items
    api.Get("/items", [pool](RequestContext& ctx) {
        ctx.Defer();  // async: suppress auto-send
        pool->Execute("SELECT id, name, price FROM items ORDER BY id", {},
            [&ctx](serverstorage::PgResult result) {
                if (!result.IsOk()) {
                    ctx.SendError(HttpStatus::kInternalServerError, "db error");
                    return;
                }
                json arr = json::array();
                for (int i = 0; i < result.RowCount(); ++i) {
                    arr.push_back({
                        {"id", result.GetInt64(i, 0)},
                        {"name", std::string(result.GetString(i, 1))},
                        {"price", result.GetInt(i, 2)}
                    });
                }
                ctx.SendJson(arr.dump());
            });
    });

    // GET /api/items/:id — get one item
    api.Get("/items/:id", [pool](RequestContext& ctx) {
        ctx.Defer();
        auto id_str = std::string(ctx.request.Param("id"));
        pool->Execute("SELECT id, name, price FROM items WHERE id = $1",
            {id_str},
            [&ctx](serverstorage::PgResult result) {
                if (!result.IsOk() || result.RowCount() == 0) {
                    ctx.SendError(HttpStatus::kNotFound, "not found");
                    return;
                }
                json obj = {
                    {"id", result.GetInt64(0, 0)},
                    {"name", std::string(result.GetString(0, 1))},
                    {"price", result.GetInt(0, 2)}
                };
                ctx.SendJson(obj.dump());
            });
    });

    // POST /api/items — create item
    api.Post("/items", [pool](RequestContext& ctx) {
        auto body = json::parse(ctx.request.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name")) {
            ctx.response.Status(HttpStatus::kBadRequest)
                .Json(R"({"error":"name required"})").Send();
            return;
        }

        ctx.Defer();
        std::string name = body["name"];
        std::string price = std::to_string(body.value("price", 0));
        pool->Execute(
            "INSERT INTO items (name, price) VALUES ($1, $2) RETURNING id, name, price",
            {name, price},
            [&ctx](serverstorage::PgResult result) {
                if (!result.IsOk() || result.RowCount() == 0) {
                    ctx.SendError(HttpStatus::kInternalServerError, "insert failed");
                    return;
                }
                json obj = {
                    {"id", result.GetInt64(0, 0)},
                    {"name", std::string(result.GetString(0, 1))},
                    {"price", result.GetInt(0, 2)}
                };
                ctx.SendJson(obj.dump(), HttpStatus::kCreated);
            });
    });

    // DELETE /api/items/:id — delete item
    api.Delete("/items/:id", [pool](RequestContext& ctx) {
        ctx.Defer();
        auto id_str = std::string(ctx.request.Param("id"));
        pool->Execute("DELETE FROM items WHERE id = $1", {id_str},
            [&ctx](serverstorage::PgResult result) {
                if (!result.IsCommand()) {
                    ctx.SendError(HttpStatus::kInternalServerError, "delete failed");
                    return;
                }
                ctx.SendJson(R"({"deleted":true})");
            });
    });
}

// ─── Main ─────────────────────────────────────────────────────────────

int main() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    serverweb::WebServerConfig config;
    config.port = 8080;
    config.worker_count = 2;

    serverweb::WebServer server(config);

    // Middleware
    server.Use(std::make_shared<serverweb::middleware::Logger>());
    server.Use(std::make_shared<serverweb::middleware::Cors>());

    // Index page
    server.Get("/", [](RequestContext& ctx) {
        ctx.response.Header("Content-Type", "text/html")
            .Body("<h1>ServerCore WebServer</h1>"
                  "<p>io_uring powered REST API</p>"
                  "<ul>"
                  "<li>GET /api/items</li>"
                  "<li>GET /api/items/:id</li>"
                  "<li>POST /api/items {\"name\":\"...\",\"price\":N}</li>"
                  "<li>DELETE /api/items/:id</li>"
                  "<li>GET /api/health</li>"
                  "</ul>")
            .Send();
    });

    server.Get("/api/health", [](RequestContext& ctx) {
        ctx.response.Json(R"({"status":"ok"})").Send();
    });

    // DB or in-memory mode
    std::shared_ptr<serverstorage::PgConnectionPool> pg_pool;
    const char* dsn = std::getenv("WEBSERVER_DB_DSN");

    if (dsn) {
        serverstorage::StorageConfig pg_cfg;
        pg_cfg.connection_string = dsn;
        BootstrapSchema(pg_cfg);

        pg_pool = std::make_shared<serverstorage::PgConnectionPool>(pg_cfg);
        bool ok = false;
        pg_pool->Initialize([&ok](bool s) { ok = s; });
        if (!ok) {
            spdlog::error("PostgreSQL pool init failed — aborting");
            return 1;
        }
        RegisterDbRoutes(server, pg_pool);
        spdlog::info("Mode: PostgreSQL ({})", dsn);
    } else {
        auto store = std::make_shared<MemoryStore>();
        RegisterMemoryRoutes(server, store);
        spdlog::info("Mode: in-memory (set WEBSERVER_DB_DSN for PostgreSQL)");
    }

    server.Start();
    spdlog::info("WebServer running on http://localhost:{}", config.port);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    server.Stop();
    return 0;
}
