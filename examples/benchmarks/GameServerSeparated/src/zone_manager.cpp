#include "zone_manager.h"

namespace bench {

ZoneManager::ZoneManager() {
    // Create default zone (lobby, id=0)
    zones_[0] = std::make_shared<Zone>(0);
}

Zone* ZoneManager::FindZone(ZoneId id) {
    std::lock_guard lock(mutex_);
    auto it = zones_.find(id);
    return it != zones_.end() ? it->second.get() : nullptr;
}

ZoneId ZoneManager::CreateZone() {
    std::lock_guard lock(mutex_);
    ZoneId id = next_zone_id_++;
    zones_[id] = std::make_shared<Zone>(id);
    return id;
}

std::vector<std::shared_ptr<Zone>> ZoneManager::GetAllZones() {
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<Zone>> result;
    result.reserve(zones_.size());
    for (auto& [id, zone] : zones_) {
        result.push_back(zone);
    }
    return result;
}

} // namespace bench
