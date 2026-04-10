#pragma once

#include <servergame/NetDispatchFn.h>
#include <servergame/PlayerRegistry.h>
#include <servergame/MatchmakerTicket.h>
#include <servergame/QueryParser.h>
#include <servercore/buffer/SendBuffer.h>
#include <servercore/job/JobQueue.h>
#include <servercore/job/JobTimer.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace servergame::match { class MatchRegistry; }

namespace servergame::matchmaker {

using servercore::PlayerId;

class MatchmakerQueue : public servercore::job::JobQueue {
public:
    MatchmakerQueue(servercore::job::GlobalQueue& gq, match::MatchRegistry& matches,
                    PlayerRegistry& registry, NetDispatcher dispatcher,
                    servercore::buffer::BufferPool& pool);

    // Thread-safe -- Push jobs for serialized execution
    void AddTicket(MatchmakerTicket ticket);
    void RemoveTicket(uint64_t ticket_id);
    void RemoveByPlayer(PlayerId pid);

    void ScheduleTick(servercore::job::JobTimer& timer,
                      std::chrono::milliseconds interval = std::chrono::seconds{1});

    /// Process one tick immediately (for testing). Must call Execute() after.
    void ProcessTickNow(servercore::job::JobTimer& timer);

    std::size_t TicketCount() const { return tickets_.size(); }

private:
    void HandleAddTicket(MatchmakerTicket ticket);
    void HandleRemoveTicket(uint64_t ticket_id);
    void HandleRemoveByPlayer(PlayerId pid);
    void ProcessTick(servercore::job::JobTimer& timer, std::chrono::milliseconds interval);
    void TryMatch(servercore::job::JobTimer& timer);
    void ExpireTimeouts();
    void NotifyMatchFound(const std::vector<MatchmakerTicket>& group, uint64_t match_id);
    void NotifyTimeout(const MatchmakerTicket& ticket);

    QueryParser::Properties TicketToProps(const MatchmakerTicket& t) const;

    std::vector<MatchmakerTicket> tickets_;
    match::MatchRegistry& match_registry_;
    PlayerRegistry& registry_;
    NetDispatcher net_dispatcher_;
    servercore::buffer::BufferPool& pool_;
    uint64_t next_ticket_id_ = 1;
};

} // namespace servergame::matchmaker
