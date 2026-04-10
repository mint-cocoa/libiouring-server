#include <servergame/NotificationService.h>
#include <servergame/WsFrameBuilder.h>

#include <nlohmann/json.hpp>

using namespace servercore;

namespace servergame::notification {

NotificationService::NotificationService(PlayerRegistry& registry,
                                         INotificationStore* store,
                                         NetDispatcher dispatcher,
                                         buffer::BufferPool& pool)
    : registry_(registry), store_(store)
    , net_dispatcher_(std::move(dispatcher)), pool_(pool) {}

void NotificationService::Send(Notification notification) {
    bool online = registry_.IsOnline(notification.recipient_id);

    if (online)
        DeliverOnline(notification.recipient_id, notification);

    if (notification.persistent && store_)
        store_->Save(notification, [](std::expected<void, GameError>) {});
}

void NotificationService::SendMulti(std::vector<PlayerId> recipients, int32_t code,
                                    const std::string& subject, const std::string& content,
                                    bool persistent) {
    for (PlayerId pid : recipients) {
        Notification n;
        n.recipient_id = pid;
        n.code = code;
        n.subject = subject;
        n.content = content;
        n.persistent = persistent;
        Send(std::move(n));
    }
}

void NotificationService::DeliverOnline(PlayerId pid, const Notification& notification) {
    auto entry = registry_.Find(pid);
    if (!entry) return;

    nlohmann::json j;
    j["type"] = "notification";
    j["id"] = notification.id;
    j["code"] = notification.code;
    j["subject"] = notification.subject;
    j["content"] = notification.content;
    if (notification.sender_id != 0) j["sender_id"] = notification.sender_id;

    auto buf = BuildWsTextFrame(pool_, j.dump());
    if (buf) {
        net_dispatcher_(entry->context_id,
                       net::SendToPlayerCmd{pid, std::move(buf)});
    }
}

} // namespace servergame::notification
