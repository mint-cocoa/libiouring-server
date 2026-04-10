#pragma once

#include "types.h"
#include "zone.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace bench {

class ZoneManager {
public:
    ZoneManager();

    Zone* FindZone(ZoneId id);
    ZoneId CreateZone();
    Zone* GetDefaultZone() { return FindZone(0); }

    // Thread-safe iteration over all zones
    template<typename Fn>
    void ForEachZone(Fn&& fn) {
        std::lock_guard lock(mutex_);
        for (auto& [id, zone] : zones_) {
            fn(*zone);
        }
    }

private:
    std::mutex mutex_;
    std::unordered_map<ZoneId, std::unique_ptr<Zone>> zones_;
    ZoneId next_zone_id_ = 1;  // 0 is default lobby
};

} // namespace bench
