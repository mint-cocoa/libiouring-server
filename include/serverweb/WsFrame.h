#pragma once
#include <cstdint>
#include <vector>

namespace serverweb::ws {

enum class WsOpcode : std::uint8_t {
    kContinuation = 0x0,
    kText         = 0x1,
    kBinary       = 0x2,
    kClose        = 0x8,
    kPing         = 0x9,
    kPong         = 0xA
};

struct WsFrame {
    bool fin = true;
    WsOpcode opcode = WsOpcode::kText;
    bool masked = false;
    std::uint64_t payload_length = 0;
    std::uint8_t masking_key[4]{};
    std::vector<std::uint8_t> payload;
};

} // namespace serverweb::ws
