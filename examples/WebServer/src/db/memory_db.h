#pragma once

#include "db/db_service.h"

#include <mutex>
#include <unordered_map>

class MemoryDb : public IDbService {
public:
    void FindAllItems(ItemListCb cb) override {
        std::vector<Item> out;
        {
            std::lock_guard lk(mu_);
            out.reserve(items_.size());
            for (auto& [_, v] : items_) out.push_back(v);
        }
        cb(std::move(out));
    }

    void FindItem(int64_t id, ItemCb cb) override {
        std::lock_guard lk(mu_);
        auto it = items_.find(id);
        if (it != items_.end())
            cb(it->second);
        else
            cb(std::nullopt);
    }

    void InsertItem(const std::string& name, int32_t price, ItemCb cb) override {
        Item item;
        {
            std::lock_guard lk(mu_);
            item = Item{next_id_++, name, price};
            items_[item.id] = item;
        }
        cb(item);
    }

    void RemoveItem(int64_t id, BoolCb cb) override {
        bool removed;
        {
            std::lock_guard lk(mu_);
            removed = items_.erase(id) > 0;
        }
        cb(removed);
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<int64_t, Item> items_;
    int64_t next_id_ = 1;
};
