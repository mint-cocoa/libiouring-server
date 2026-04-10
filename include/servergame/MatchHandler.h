#pragma once

#include <servercore/Types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace servergame::match {

using servercore::PlayerId;

class Match; // forward decl

class MatchHandler {
public:
    virtual ~MatchHandler() = default;

    virtual void Init(Match& match) = 0;
    virtual bool JoinAttempt(Match& match, PlayerId player_id) { (void)match; (void)player_id; return true; }
    virtual void OnJoin(Match& match, PlayerId player_id) = 0;
    virtual void OnLeave(Match& match, PlayerId player_id) = 0;
    virtual void OnMessage(Match& match, PlayerId sender,
                           int64_t opcode, std::span<const std::byte> data) = 0;
    virtual void OnTick(Match& match, std::chrono::milliseconds dt) = 0;
    virtual void OnTerminate(Match& match) { (void)match; }
};

} // namespace servergame::match
