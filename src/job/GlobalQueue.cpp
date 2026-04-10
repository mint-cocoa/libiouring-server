#include <servercore/job/GlobalQueue.h>

namespace servercore::job {

void GlobalQueue::Push(JobQueue* jq) {
    std::lock_guard lk(mutex_);
    queue_.push_back(jq);
}

JobQueue* GlobalQueue::TryPop() {
    std::lock_guard lk(mutex_);
    if (queue_.empty())
        return nullptr;
    JobQueue* jq = queue_.front();
    queue_.pop_front();
    return jq;
}

} // namespace servercore::job
