#pragma once

#include "io_worker.h"
#include <vector>
#include <memory>

class IoWorkerPool {
public:
    explicit IoWorkerPool(std::uint16_t count);

    void StopAll();

    IoWorker*                         GetWorker(servercore::ContextId id);
    servercore::job::GlobalQueue&     GetGlobalQueue() { return global_queue_; }
    servercore::job::JobTimer&        GetTimer()       { return timer_; }
    std::uint16_t                     Count() const    { return static_cast<std::uint16_t>(workers_.size()); }

private:
    std::vector<std::unique_ptr<IoWorker>> workers_;
    servercore::job::GlobalQueue global_queue_;
    servercore::job::JobTimer timer_;
};
