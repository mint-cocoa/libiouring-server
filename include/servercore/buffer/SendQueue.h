#pragma once

#include <servercore/buffer/SendBuffer.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace servercore::buffer {

struct PushResult {
    bool needs_register;  // First push since last drain → caller must register send
    bool overflowed;      // Queue capacity exceeded
};

class SendQueue {
public:
    explicit SendQueue(std::uint32_t max_pending = 4096)
        : max_pending_(max_pending) {
        pending_.reserve(32);
    }

    // Push a send buffer. Returns flags decided atomically under one lock.
    PushResult Push(SendBufferRef buf) {
        std::lock_guard lk(mutex_);
        bool overflow = pending_.size() >= max_pending_;
        if (overflow)
            return {false, true};

        bool first = !registered_;
        pending_.push_back(std::move(buf));
        registered_ = true;
        return {first, false};
    }

    // Drain all pending buffers (called from IO thread after send completes).
    std::vector<SendBufferRef> Drain() {
        std::lock_guard lk(mutex_);
        std::vector<SendBufferRef> out;
        out.swap(pending_);
        return out;
    }

    // Mark send cycle as complete — next Push will set needs_register again.
    void MarkSent() {
        std::lock_guard lk(mutex_);
        if (pending_.empty())
            registered_ = false;
    }

private:
    std::mutex mutex_;
    std::vector<SendBufferRef> pending_;
    bool registered_ = false;
    std::uint32_t max_pending_;
};

} // namespace servercore::buffer
