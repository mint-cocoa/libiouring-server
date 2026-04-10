#pragma once

#include "model/item.h"

#include <functional>
#include <optional>
#include <vector>

// Abstract data-access interface for Item CRUD.
// Implementations: MemoryDb (in-memory), PgDb (PostgreSQL async).
class IDbService {
public:
    virtual ~IDbService() = default;

    using ItemListCb  = std::function<void(std::vector<Item>)>;
    using ItemCb      = std::function<void(std::optional<Item>)>;
    using BoolCb      = std::function<void(bool)>;

    virtual void FindAllItems(ItemListCb cb) = 0;
    virtual void FindItem(int64_t id, ItemCb cb) = 0;
    virtual void InsertItem(const std::string& name, int32_t price, ItemCb cb) = 0;
    virtual void RemoveItem(int64_t id, BoolCb cb) = 0;
};
