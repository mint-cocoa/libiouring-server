#pragma once

#include "db/db_service.h"

#include <memory>

// Business-logic layer for Item operations.
// Delegates persistence to IDbService — no SQL here.
class ItemService {
public:
    explicit ItemService(std::shared_ptr<IDbService> db) : db_(std::move(db)) {}

    void ListItems(IDbService::ItemListCb cb) { db_->FindAllItems(std::move(cb)); }
    void GetItem(int64_t id, IDbService::ItemCb cb) { db_->FindItem(id, std::move(cb)); }
    void CreateItem(const std::string& name, int32_t price, IDbService::ItemCb cb) {
        db_->InsertItem(name, price, std::move(cb));
    }
    void DeleteItem(int64_t id, IDbService::BoolCb cb) { db_->RemoveItem(id, std::move(cb)); }

private:
    std::shared_ptr<IDbService> db_;
};
