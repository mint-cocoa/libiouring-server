#include "handler/item_handler.h"
#include "model/item.h"

#include <serverweb/HttpResponse.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using serverweb::RequestContext;
using serverweb::HttpStatus;

void ItemHandler::Register(serverweb::WebServer& server) {
    auto api = server.Group("/api");
    auto svc = svc_;  // capture shared_ptr by value for lambda lifetime

    // GET /api/items
    api.Get("/items", [svc](RequestContext& ctx) {
        ctx.Defer();
        svc->ListItems([&ctx](std::vector<Item> items) {
            json arr = json::array();
            for (auto& it : items) arr.push_back(ItemToJson(it));
            ctx.SendJson(arr.dump());
        });
    });

    // GET /api/items/:id
    api.Get("/items/:id", [svc](RequestContext& ctx) {
        ctx.Defer();
        auto id_str = ctx.request.Param("id");
        int64_t id = std::atoll(std::string(id_str).c_str());
        svc->GetItem(id, [&ctx](std::optional<Item> item) {
            if (!item) {
                ctx.SendError(HttpStatus::kNotFound, "not found");
                return;
            }
            ctx.SendJson(ItemToJson(*item).dump());
        });
    });

    // POST /api/items
    api.Post("/items", [svc](RequestContext& ctx) {
        auto body = json::parse(ctx.request.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name")) {
            ctx.response.Status(HttpStatus::kBadRequest)
                .Json(R"({"error":"name required"})").Send();
            return;
        }

        ctx.Defer();
        std::string name = body["name"];
        int32_t price = body.value("price", 0);
        svc->CreateItem(name, price, [&ctx](std::optional<Item> item) {
            if (!item) {
                ctx.SendError(HttpStatus::kInternalServerError, "insert failed");
                return;
            }
            ctx.SendJson(ItemToJson(*item).dump(), HttpStatus::kCreated);
        });
    });

    // DELETE /api/items/:id
    api.Delete("/items/:id", [svc](RequestContext& ctx) {
        ctx.Defer();
        auto id_str = ctx.request.Param("id");
        int64_t id = std::atoll(std::string(id_str).c_str());
        svc->DeleteItem(id, [&ctx](bool deleted) {
            if (!deleted) {
                ctx.SendError(HttpStatus::kNotFound, "not found");
                return;
            }
            ctx.SendJson(R"({"deleted":true})");
        });
    });
}
