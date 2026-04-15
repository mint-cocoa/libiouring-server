#include <serverweb/UpstreamPool.h>

#include <memory>

namespace serverweb {

UpstreamPool::UpstreamPool(std::size_t max_idle_per_target,
                           std::chrono::seconds idle_timeout)
    : max_idle_per_target_(max_idle_per_target)
    , idle_timeout_(idle_timeout)
{}

void UpstreamPool::Forward(servercore::ring::IoRing& ring,
                           servercore::buffer::BufferPool& pool,
                           const UpstreamTarget& target,
                           std::string request_bytes,
                           ProxyCallback on_response,
                           ProxyErrorCallback on_error)
{
    auto session = std::make_shared<UpstreamSession>(ring, pool);
    session->Init();

    {
        std::lock_guard<std::mutex> lock(mu_);
        ++active_count_;
    }

    // Wrap callbacks to decrement active_count_ and keep session alive
    auto wrapped_response = [this, session, cb = std::move(on_response)](
        int status_code,
        std::vector<std::pair<std::string, std::string>> headers,
        std::vector<std::byte> body) mutable
    {
        cb(status_code, std::move(headers), std::move(body));
        std::lock_guard<std::mutex> lock(mu_);
        --active_count_;
    };

    auto wrapped_error = [this, session, cb = std::move(on_error)](
        std::string error) mutable
    {
        cb(std::move(error));
        std::lock_guard<std::mutex> lock(mu_);
        --active_count_;
    };

    session->Connect(target.host, target.port,
                     std::move(request_bytes),
                     std::move(wrapped_response),
                     std::move(wrapped_error));
}

std::size_t UpstreamPool::IdleCount(const std::string& /*key*/) const
{
    // No idle pool implemented yet — always 0
    return 0;
}

std::size_t UpstreamPool::ActiveCount() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return active_count_;
}

} // namespace serverweb
