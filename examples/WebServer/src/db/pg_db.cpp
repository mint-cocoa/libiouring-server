#include "db/pg_db.h"

#include <serverstorage/PgConnection.h>
#include <spdlog/spdlog.h>

void PgDb::BootstrapSchema(const serverstorage::StorageConfig& cfg) {
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

void PgDb::FindAllItems(ItemListCb cb) {
    pool_->Execute("SELECT id, name, price FROM items ORDER BY id", {},
        [cb = std::move(cb)](serverstorage::PgResult result) {
            std::vector<Item> items;
            if (result.IsOk()) {
                for (int i = 0; i < result.RowCount(); ++i) {
                    items.push_back({
                        result.GetInt64(i, 0),
                        std::string(result.GetString(i, 1)),
                        result.GetInt(i, 2)
                    });
                }
            }
            cb(std::move(items));
        });
}

void PgDb::FindItem(int64_t id, ItemCb cb) {
    pool_->Execute("SELECT id, name, price FROM items WHERE id = $1",
        {std::to_string(id)},
        [cb = std::move(cb)](serverstorage::PgResult result) {
            if (result.IsOk() && result.RowCount() > 0) {
                cb(Item{
                    result.GetInt64(0, 0),
                    std::string(result.GetString(0, 1)),
                    result.GetInt(0, 2)
                });
            } else {
                cb(std::nullopt);
            }
        });
}

void PgDb::InsertItem(const std::string& name, int32_t price, ItemCb cb) {
    pool_->Execute(
        "INSERT INTO items (name, price) VALUES ($1, $2) RETURNING id, name, price",
        {name, std::to_string(price)},
        [cb = std::move(cb)](serverstorage::PgResult result) {
            if (result.IsOk() && result.RowCount() > 0) {
                cb(Item{
                    result.GetInt64(0, 0),
                    std::string(result.GetString(0, 1)),
                    result.GetInt(0, 2)
                });
            } else {
                cb(std::nullopt);
            }
        });
}

void PgDb::RemoveItem(int64_t id, BoolCb cb) {
    pool_->Execute("DELETE FROM items WHERE id = $1",
        {std::to_string(id)},
        [cb = std::move(cb)](serverstorage::PgResult result) {
            cb(result.IsCommand());
        });
}
