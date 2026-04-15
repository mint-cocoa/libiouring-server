#pragma once

#include <serverweb/UpstreamSession.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace serverweb {

struct UpstreamTarget {
    std::string host;
    std::uint16_t port;
    std::string Key() const { return host + ":" + std::to_string(port); }
};

class UpstreamPool {
public:
    explicit UpstreamPool(std::size_t max_idle_per_target = 4,
                          std::chrono::seconds idle_timeout = std::chrono::seconds(60));

    // Forward an HTTP request to upstream. Creates a new UpstreamSession each time.
    // (Connection pooling/reuse is a future optimization)
    void Forward(servercore::ring::IoRing& ring,
                 servercore::buffer::BufferPool& pool,
                 const UpstreamTarget& target,
                 std::string request_bytes,
                 ProxyCallback on_response,
                 ProxyErrorCallback on_error);

    std::size_t IdleCount(const std::string& key) const;
    std::size_t ActiveCount() const;

private:
    std::size_t max_idle_per_target_;
    std::chrono::seconds idle_timeout_;

    mutable std::mutex mu_;
    std::size_t active_count_ = 0;
};

} // namespace serverweb
