#pragma once

#include <servergame/Notification.h>
#include <servergame/Error.h>

#include <expected>
#include <functional>

namespace servergame::notification {

class INotificationStore {
public:
    virtual ~INotificationStore() = default;
    virtual void Save(const Notification& notif,
                      std::move_only_function<void(std::expected<void, GameError>)> cb) = 0;
};

} // namespace servergame::notification
