#pragma once

#include <chrono>

inline void BusyWork(std::chrono::microseconds us) {
    if (us.count() <= 0)
        return;
    auto end = std::chrono::steady_clock::now() + us;
    while (std::chrono::steady_clock::now() < end)
        asm volatile("" ::: "memory");
}
