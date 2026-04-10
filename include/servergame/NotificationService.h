#pragma once

#include <servergame/NetDispatchFn.h>
#include <servergame/PlayerRegistry.h>
#include <servergame/Notification.h>
#include <servergame/INotificationStore.h>
#include <servercore/buffer/SendBuffer.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace servergame::notification {

using servercore::PlayerId;

class NotificationService {
public:
    /// @param store Optional -- if null, persistence is disabled.
    NotificationService(PlayerRegistry& registry,
                        INotificationStore* store,
                        NetDispatcher dispatcher,
                        servercore::buffer::BufferPool& pool);

    void Send(Notification notification);
    void SendMulti(std::vector<PlayerId> recipients, int32_t code,
                   const std::string& subject, const std::string& content,
                   bool persistent = true);

private:
    void DeliverOnline(PlayerId pid, const Notification& notification);

    PlayerRegistry& registry_;
    INotificationStore* store_;
    NetDispatcher net_dispatcher_;
    servercore::buffer::BufferPool& pool_;
};

} // namespace servergame::notification
