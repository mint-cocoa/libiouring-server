#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace serverstorage {

struct StorageConfig {
    std::string connection_string = "host=localhost dbname=gamedb user=game";
    std::uint16_t connections_per_worker = 2;
    std::uint32_t query_timeout_ms = 5000;
    std::uint32_t connect_timeout_ms = 3000;
};

} // namespace serverstorage
