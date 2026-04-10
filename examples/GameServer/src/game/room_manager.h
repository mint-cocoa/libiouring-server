#pragma once

#include "../types.h"
#include "room.h"
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <vector>

class IoWorkerPool;

class RoomManager {
public:
    RoomManager(servercore::job::GlobalQueue& gq, IoWorkerPool* workers,
                servercore::job::JobTimer& timer);

    Room* CreateRoom(const std::string& name);
    Room* FindRoom(RoomId id);
    void  RemoveRoom(RoomId id);
    void  CleanupEmptyRooms();
    RoomId NextId() const { return next_id_; }

    struct RoomInfo {
        RoomId id;
        std::string name;
        std::uint32_t player_count;
    };
    std::vector<RoomInfo> GetRoomList();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<RoomId, std::shared_ptr<Room>> rooms_;
    RoomId next_id_ = 1;
    servercore::job::GlobalQueue& global_queue_;
    servercore::job::JobTimer& timer_;
    IoWorkerPool* workers_;
};
