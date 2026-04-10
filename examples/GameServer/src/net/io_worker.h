#pragma once

#include <servercore/ring/IoRing.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/io/Listener.h>
#include <servercore/io/Session.h>
#include <servercore/job/JobQueue.h>
#include <servercore/job/JobTimer.h>
#include <servercore/job/GlobalQueue.h>
#include <servercore/Types.h>
#include <thread>
#include <atomic>
#include <unordered_map>

class IoWorker {
public:
    IoWorker(servercore::ContextId id,
             servercore::job::GlobalQueue& global_queue,
             servercore::job::JobTimer& timer);

    void Start(const servercore::Address& addr,
               servercore::io::SessionFactory factory);
    void Stop();

    servercore::ContextId             Id()   const { return id_; }
    servercore::ring::IoRing*         Ring()       { return ring_.get(); }
    servercore::buffer::BufferPool&   Pool()       { return pool_; }

    void AddSession(servercore::SessionId sid, servercore::io::SessionRef session);
    void RemoveSession(servercore::SessionId sid);
    servercore::io::Session* FindSession(servercore::SessionId sid);

private:
    void Run();

    servercore::ContextId id_;
    std::unique_ptr<servercore::ring::IoRing> ring_;
    servercore::buffer::BufferPool pool_;
    std::shared_ptr<servercore::io::Listener> listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::unordered_map<servercore::SessionId, servercore::io::SessionRef> sessions_;

    servercore::job::GlobalQueue& global_queue_;
    servercore::job::JobTimer& timer_;
};
