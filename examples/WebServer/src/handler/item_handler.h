#pragma once

#include "service/item_service.h"

#include <serverweb/WebServer.h>

#include <memory>

class ItemHandler {
public:
    explicit ItemHandler(std::shared_ptr<ItemService> svc) : svc_(std::move(svc)) {}
    void Register(serverweb::WebServer& server);

private:
    std::shared_ptr<ItemService> svc_;
};
