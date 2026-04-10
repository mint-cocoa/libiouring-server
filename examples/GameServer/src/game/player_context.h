#pragma once

#include "../types.h"
#include <servercore/Types.h>
#include <servercore/ring/IoRing.h>
#include <servercore/io/Session.h>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <memory>

class Room;

struct PlayerContext {
    servercore::SessionId session_id = 0;
    PlayerId player_id = 0;
    std::string username;
    CharId selected_char_id = 0;
    std::string char_name;
    int32_t level = 1;

    Room* room = nullptr;

    servercore::ring::IoRing* worker_ring = nullptr;
    servercore::ContextId worker_id = 0;
    std::weak_ptr<servercore::io::Session> session;
};

class PlayerManager {
public:
    PlayerContext* Register(servercore::SessionId sid, PlayerId pid,
                            servercore::ring::IoRing* ring,
                            servercore::ContextId worker_id,
                            std::weak_ptr<servercore::io::Session> sess);

    void Unregister(servercore::SessionId sid);

    PlayerContext* FindBySession(servercore::SessionId sid);
    PlayerContext* FindByPlayer(PlayerId pid);
    PlayerContext* FindByName(const std::string& name);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<servercore::SessionId, std::unique_ptr<PlayerContext>> by_session_;
    std::unordered_map<PlayerId, PlayerContext*> by_player_;
    std::unordered_map<std::string, PlayerContext*> by_name_;
};
