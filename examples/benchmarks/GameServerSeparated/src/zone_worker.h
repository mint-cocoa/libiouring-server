#pragma once

#include "zone.h"

#include <servercore/buffer/SendBuffer.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace bench {

class ZoneWorker {
public:
    explicit ZoneWorker(int worker_id)
        : worker_id_(worker_id) {}

    ~ZoneWorker() { Stop(); }

    ZoneWorker(const ZoneWorker&) = delete;
    ZoneWorker& operator=(const ZoneWorker&) = delete;

    void AddZone(std::shared_ptr<Zone> zone) {
        zones_.push_back(std::move(zone));
    }

    void Start(std::atomic<bool>& running);
    void Stop();

private:
    void Run(std::atomic<bool>& running);

    int worker_id_;
    std::vector<std::shared_ptr<Zone>> zones_;
    std::thread thread_;
};

} // namespace bench
