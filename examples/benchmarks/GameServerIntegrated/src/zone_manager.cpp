#include "zone_manager.h"

namespace bench {

ZoneManager::ZoneManager() {
    // Create default zone (lobby, id=0)
    zones_[0] = std::make_unique<Zone>(0);
}

Zone* ZoneManager::FindZone(ZoneId id) {
    std::lock_guard lock(mutex_);
    auto it = zones_.find(id);
    return it != zones_.end() ? it->second.get() : nullptr;
}

ZoneId ZoneManager::CreateZone() {
    std::lock_guard lock(mutex_);
    ZoneId id = next_zone_id_++;
    zones_[id] = std::make_unique<Zone>(id);
    return id;
}

} // namespace bench
