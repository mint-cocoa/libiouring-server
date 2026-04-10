#pragma once
#include <cstdint>
#include <chrono>

namespace bench {

using PlayerId  = std::uint64_t;
using ZoneId    = std::uint32_t;
using WorkerId  = std::uint16_t;
using TimePoint = std::chrono::steady_clock::time_point;

} // namespace bench
