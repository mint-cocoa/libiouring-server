#pragma once

#include "db/db_service.h"

#include <serverstorage/PgConnectionPool.h>
#include <serverstorage/StorageConfig.h>

#include <memory>

class PgDb : public IDbService {
public:
    explicit PgDb(std::shared_ptr<serverstorage::PgConnectionPool> pool)
        : pool_(std::move(pool)) {}

    void FindAllItems(ItemListCb cb) override;
    void FindItem(int64_t id, ItemCb cb) override;
    void InsertItem(const std::string& name, int32_t price, ItemCb cb) override;
    void RemoveItem(int64_t id, BoolCb cb) override;

    // Create items table if it doesn't exist (synchronous, call before pool init).
    static void BootstrapSchema(const serverstorage::StorageConfig& cfg);

private:
    std::shared_ptr<serverstorage::PgConnectionPool> pool_;
};
