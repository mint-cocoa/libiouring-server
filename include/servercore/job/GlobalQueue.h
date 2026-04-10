#pragma once

#include <deque>
#include <mutex>

namespace servercore::job {

class JobQueue;

class GlobalQueue {
public:
    void Push(JobQueue* jq);
    JobQueue* TryPop();

private:
    std::mutex mutex_;
    std::deque<JobQueue*> queue_;
};

} // namespace servercore::job
