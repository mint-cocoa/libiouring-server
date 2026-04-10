#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

struct Item {
    int64_t id = 0;
    std::string name;
    int32_t price = 0;
};

inline nlohmann::json ItemToJson(const Item& it) {
    return {{"id", it.id}, {"name", it.name}, {"price", it.price}};
}
