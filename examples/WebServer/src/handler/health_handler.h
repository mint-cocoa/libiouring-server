#pragma once

#include <serverweb/WebServer.h>

#include <string>

class HealthHandler {
public:
    explicit HealthHandler(std::string mode) : mode_(std::move(mode)) {}
    void Register(serverweb::WebServer& server);

private:
    std::string mode_;
};
