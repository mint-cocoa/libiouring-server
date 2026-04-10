#include "pg_db_service.h"

#include <serverstorage/PgConnection.h>
#include <serverstorage/PgResult.h>
#include <spdlog/spdlog.h>

#include <utility>

using serverstorage::PgResult;

namespace {

// DDL statements idempotently create the schema used by the SQLite
// implementation, translated to PostgreSQL dialect.
constexpr const char* kDdlAccounts =
    "CREATE TABLE IF NOT EXISTS accounts ("
    "  id BIGSERIAL PRIMARY KEY,"
    "  username TEXT UNIQUE NOT NULL,"
    "  password TEXT NOT NULL"
    ")";

constexpr const char* kDdlCharacters =
    "CREATE TABLE IF NOT EXISTS characters ("
    "  id BIGSERIAL PRIMARY KEY,"
    "  player_id BIGINT NOT NULL,"
    "  name TEXT UNIQUE NOT NULL,"
    "  level INTEGER NOT NULL DEFAULT 1"
    ")";

constexpr const char* kDdlCurrency =
    "CREATE TABLE IF NOT EXISTS currency ("
    "  char_id BIGINT PRIMARY KEY,"
    "  gold BIGINT NOT NULL DEFAULT 1000,"
    "  gems BIGINT NOT NULL DEFAULT 50,"
    "  tokens BIGINT NOT NULL DEFAULT 10"
    ")";

constexpr const char* kDdlInventory =
    "CREATE TABLE IF NOT EXISTS inventory ("
    "  instance_id BIGSERIAL PRIMARY KEY,"
    "  char_id BIGINT NOT NULL,"
    "  item_def_id INTEGER NOT NULL,"
    "  slot INTEGER NOT NULL,"
    "  quantity INTEGER NOT NULL DEFAULT 1,"
    "  durability INTEGER NOT NULL DEFAULT 100"
    ")";

} // namespace

PgDbService::PgDbService(std::shared_ptr<serverstorage::PgConnectionPool> pool,
                          serverstorage::StorageConfig config)
    : pool_(std::move(pool)), config_(std::move(config)) {
    InitSchema();
}

void PgDbService::InitSchema() {
    // Schema creation runs on whatever thread constructed this service —
    // typically main() before any IoWorker has started. We cannot use the
    // worker pool here because PgWorkerPool::Submit requires an active
    // IoRing on the calling thread. Instead we open a short-lived private
    // connection and execute the DDL synchronously.
    serverstorage::PgConnection bootstrap(config_);
    if (!bootstrap.Connect()) {
        spdlog::error("PgDbService::InitSchema: bootstrap connect failed");
        return;
    }

    for (const char* ddl : {kDdlAccounts, kDdlCharacters, kDdlCurrency, kDdlInventory}) {
        auto res = bootstrap.Execute(ddl, {});
        if (!res) {
            spdlog::error("PgDbService::InitSchema: DDL failed: {}", ddl);
        }
    }
    spdlog::info("PgDbService: schema initialized");
}

// -- Login (with auto-register fallback) --------------------------------

void PgDbService::Login(const std::string& username, const std::string& password,
                         std::function<void(bool, PlayerId)> cb) {
    auto pool = pool_;
    pool->Execute(
        "SELECT id, password FROM accounts WHERE username = $1",
        {username},
        [pool, username, password, cb = std::move(cb)](PgResult result) mutable {
            if (!result.IsOk()) {
                spdlog::error("PgDbService::Login: SELECT failed: {}", result.ErrorMessage());
                cb(false, 0);
                return;
            }

            if (result.RowCount() > 0) {
                PlayerId id = result.GetInt64(0, 0);
                std::string stored(result.GetString(0, 1));
                bool ok = (stored == password);
                cb(ok, ok ? id : 0);
                return;
            }

            // No account — auto-register, matching SqliteDbService semantics.
            pool->Execute(
                "INSERT INTO accounts (username, password) VALUES ($1, $2) RETURNING id",
                {username, password},
                [cb = std::move(cb)](PgResult insert_result) {
                    if (!insert_result.IsOk() || insert_result.RowCount() == 0) {
                        spdlog::error("PgDbService::Login: auto-register failed: {}",
                                      insert_result.ErrorMessage());
                        cb(false, 0);
                        return;
                    }
                    PlayerId new_id = insert_result.GetInt64(0, 0);
                    cb(true, new_id);
                });
        });
}

// -- Register ----------------------------------------------------------

void PgDbService::Register(const std::string& username, const std::string& password,
                            std::function<void(bool, std::string)> cb) {
    pool_->Execute(
        "INSERT INTO accounts (username, password) VALUES ($1, $2)",
        {username, password},
        [cb = std::move(cb)](PgResult result) {
            if (!result.IsCommand()) {
                // Most common failure is UNIQUE violation on username.
                cb(false, "Username already exists");
                return;
            }
            cb(true, "");
        });
}

// -- GetCharList -------------------------------------------------------

void PgDbService::GetCharList(PlayerId pid,
                               std::function<void(std::vector<CharInfo>)> cb) {
    pool_->Execute(
        "SELECT id, name, level FROM characters WHERE player_id = $1",
        {std::to_string(pid)},
        [cb = std::move(cb)](PgResult result) {
            std::vector<CharInfo> chars;
            if (!result.IsOk()) {
                spdlog::error("PgDbService::GetCharList: SELECT failed: {}", result.ErrorMessage());
                cb(std::move(chars));
                return;
            }
            const int rows = result.RowCount();
            chars.reserve(rows);
            for (int i = 0; i < rows; ++i) {
                CharInfo ci;
                ci.id = result.GetInt64(i, 0);
                ci.name = std::string(result.GetString(i, 1));
                ci.level = result.GetInt(i, 2);
                chars.push_back(std::move(ci));
            }
            cb(std::move(chars));
        });
}

// -- CreateChar (two-step: insert character → seed currency) -----------

void PgDbService::CreateChar(PlayerId pid, const std::string& name,
                              std::function<void(bool, std::string, CharInfo)> cb) {
    auto pool = pool_;
    pool->Execute(
        "INSERT INTO characters (player_id, name, level) VALUES ($1, $2, 1) RETURNING id",
        {std::to_string(pid), name},
        [pool, name, cb = std::move(cb)](PgResult result) mutable {
            if (!result.IsOk() || result.RowCount() == 0) {
                cb(false, "Character name already exists", CharInfo{});
                return;
            }

            CharInfo ci;
            ci.id = result.GetInt64(0, 0);
            ci.name = name;
            ci.level = 1;

            // Seed default currency row; ON CONFLICT DO NOTHING is the
            // PostgreSQL equivalent of SQLite's INSERT OR IGNORE.
            pool->Execute(
                "INSERT INTO currency (char_id, gold) VALUES ($1, 1000) "
                "ON CONFLICT (char_id) DO NOTHING",
                {std::to_string(ci.id)},
                [ci, cb = std::move(cb)](PgResult) {
                    // Ignore seed failures — the character row is already
                    // committed, and LoadCurrency will insert on demand.
                    cb(true, "", ci);
                });
        });
}

// -- LoadInventory -----------------------------------------------------

void PgDbService::LoadInventory(CharId cid,
                                 std::function<void(std::vector<ItemData>)> cb) {
    pool_->Execute(
        "SELECT instance_id, item_def_id, slot, quantity, durability "
        "FROM inventory WHERE char_id = $1",
        {std::to_string(cid)},
        [cb = std::move(cb)](PgResult result) {
            std::vector<ItemData> items;
            if (!result.IsOk()) {
                spdlog::error("PgDbService::LoadInventory: SELECT failed: {}",
                              result.ErrorMessage());
                cb(std::move(items));
                return;
            }
            const int rows = result.RowCount();
            items.reserve(rows);
            for (int i = 0; i < rows; ++i) {
                ItemData it;
                it.instance_id = result.GetInt64(i, 0);
                it.item_def_id = result.GetInt(i, 1);
                it.slot = result.GetInt(i, 2);
                it.quantity = result.GetInt(i, 3);
                it.durability = result.GetInt(i, 4);
                items.push_back(it);
            }
            cb(std::move(items));
        });
}

// -- LoadCurrency (select, insert default on miss) ---------------------

void PgDbService::LoadCurrency(CharId cid,
                                std::function<void(CurrencyData)> cb) {
    auto pool = pool_;
    pool->Execute(
        "SELECT gold FROM currency WHERE char_id = $1",
        {std::to_string(cid)},
        [pool, cid, cb = std::move(cb)](PgResult result) mutable {
            if (result.IsOk() && result.RowCount() > 0) {
                CurrencyData data;
                data.gold = result.GetInt64(0, 0);
                cb(data);
                return;
            }

            // Row missing — seed default and return default to caller.
            pool->Execute(
                "INSERT INTO currency (char_id, gold) VALUES ($1, 1000) "
                "ON CONFLICT (char_id) DO NOTHING",
                {std::to_string(cid)},
                [cb = std::move(cb)](PgResult) {
                    CurrencyData data;
                    data.gold = 1000;
                    cb(data);
                });
        });
}
