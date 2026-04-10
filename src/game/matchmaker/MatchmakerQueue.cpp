#include <servergame/MatchmakerQueue.h>
#include <servergame/MatchRegistry.h>

#include <algorithm>
#include <cstring>

using namespace servercore;

namespace servergame::matchmaker {

MatchmakerQueue::MatchmakerQueue(job::GlobalQueue& gq, match::MatchRegistry& matches,
                                 PlayerRegistry& registry, NetDispatcher dispatcher,
                                 buffer::BufferPool& pool)
    : JobQueue(gq)
    , match_registry_(matches)
    , registry_(registry)
    , net_dispatcher_(std::move(dispatcher))
    , pool_(pool) {}

void MatchmakerQueue::AddTicket(MatchmakerTicket ticket) {
    Push([this, t = std::move(ticket)]() mutable { HandleAddTicket(std::move(t)); });
}

void MatchmakerQueue::RemoveTicket(uint64_t ticket_id) {
    Push([this, ticket_id] { HandleRemoveTicket(ticket_id); });
}

void MatchmakerQueue::RemoveByPlayer(PlayerId pid) {
    Push([this, pid] { HandleRemoveByPlayer(pid); });
}

void MatchmakerQueue::ScheduleTick(job::JobTimer& timer,
                                   std::chrono::milliseconds interval) {
    timer.Reserve(interval, weak_from_this(), [this, &timer, interval] {
        ProcessTick(timer, interval);
    });
}

void MatchmakerQueue::ProcessTickNow(job::JobTimer& timer) {
    Push([this, &timer] {
        ExpireTimeouts();
        TryMatch(timer);
    });
}

void MatchmakerQueue::HandleAddTicket(MatchmakerTicket ticket) {
    ticket.ticket_id = next_ticket_id_++;
    if (ticket.submitted_at == std::chrono::steady_clock::time_point{})
        ticket.submitted_at = std::chrono::steady_clock::now();
    tickets_.push_back(std::move(ticket));
}

void MatchmakerQueue::HandleRemoveTicket(uint64_t ticket_id) {
    std::erase_if(tickets_, [ticket_id](const auto& t) {
        return t.ticket_id == ticket_id;
    });
}

void MatchmakerQueue::HandleRemoveByPlayer(PlayerId pid) {
    std::erase_if(tickets_, [pid](const auto& t) {
        return t.player_id == pid;
    });
}

void MatchmakerQueue::ProcessTick(job::JobTimer& timer,
                                  std::chrono::milliseconds interval) {
    ExpireTimeouts();
    TryMatch(timer);
    ScheduleTick(timer, interval);
}

void MatchmakerQueue::TryMatch(job::JobTimer& timer) {
    if (tickets_.size() < 2) return;

    struct ParsedTicket {
        std::size_t index;
        std::vector<QueryParser::Condition> conditions;
        QueryParser::Properties props;
    };

    std::vector<ParsedTicket> parsed;
    parsed.reserve(tickets_.size());
    for (std::size_t i = 0; i < tickets_.size(); ++i) {
        parsed.push_back({
            i,
            QueryParser::Parse(tickets_[i].query),
            TicketToProps(tickets_[i]),
        });
    }

    std::vector<bool> used(tickets_.size(), false);

    for (std::size_t i = 0; i < parsed.size(); ++i) {
        if (used[i]) continue;

        auto& ti = tickets_[i];
        std::vector<std::size_t> group = {i};

        for (std::size_t j = i + 1; j < parsed.size(); ++j) {
            if (used[j]) continue;
            if (tickets_[j].handler_name != ti.handler_name) continue;

            bool i_accepts_j = QueryParser::Evaluate(parsed[i].conditions, parsed[j].props);
            bool j_accepts_i = QueryParser::Evaluate(parsed[j].conditions, parsed[i].props);

            if (i_accepts_j && j_accepts_i) {
                group.push_back(j);
                if (group.size() >= ti.max_count) break;
            }
        }

        if (group.size() >= ti.min_count) {
            match::MatchConfig cfg;
            cfg.handler_name = ti.handler_name;
            auto result = match_registry_.CreateMatch(ti.handler_name, cfg, timer);
            if (!result) continue;
            auto* match = *result;

            std::vector<MatchmakerTicket> matched;
            for (auto idx : group) {
                used[idx] = true;
                if (match) match->RequestJoin(tickets_[idx].player_id, tickets_[idx].context_id);
                matched.push_back(tickets_[idx]);
            }
            NotifyMatchFound(matched, match->Id());
        }
    }

    std::vector<MatchmakerTicket> remaining;
    for (std::size_t i = 0; i < tickets_.size(); ++i) {
        if (!used[i]) remaining.push_back(std::move(tickets_[i]));
    }
    tickets_ = std::move(remaining);
}

void MatchmakerQueue::ExpireTimeouts() {
    auto now = std::chrono::steady_clock::now();
    std::vector<MatchmakerTicket> expired;

    auto it = std::remove_if(tickets_.begin(), tickets_.end(),
        [&](const auto& t) {
            if (now - t.submitted_at >= t.timeout) {
                expired.push_back(t);
                return true;
            }
            return false;
        });
    tickets_.erase(it, tickets_.end());

    for (auto& t : expired)
        NotifyTimeout(t);
}

void MatchmakerQueue::NotifyMatchFound(const std::vector<MatchmakerTicket>& group,
                                       uint64_t match_id) {
    // Build a binary notification: [match_id:u64] [player_count:u8] [player_ids:u64...]
    // Wrapped in a 4-byte PacketHeader (size + id=0x3001 kMatchFound)
    for (auto& t : group) {
        auto entry = registry_.Find(t.player_id);
        if (!entry) continue;

        // Calculate sizes
        uint8_t player_count = static_cast<uint8_t>(group.size());
        uint16_t body_size = 8 + 1 + static_cast<uint16_t>(player_count) * 8;
        uint16_t total_size = 4 + body_size; // header + body
        uint16_t packet_id = 0x3001; // kMatchFound

        auto buf_result = pool_.Allocate(total_size);
        if (!buf_result) continue;
        auto& buf = *buf_result;

        auto writable = buf->Writable();
        uint8_t* p = reinterpret_cast<uint8_t*>(writable.data());

        // Header
        std::memcpy(p, &total_size, 2); p += 2;
        std::memcpy(p, &packet_id, 2); p += 2;

        // Body: match_id
        std::memcpy(p, &match_id, 8); p += 8;

        // Body: player_count + player_ids
        *p++ = player_count;
        for (auto& pt : group) {
            uint64_t pid = pt.player_id;
            std::memcpy(p, &pid, 8); p += 8;
        }

        buf->Commit(total_size);
        net_dispatcher_(entry->context_id,
                       net::SendToPlayerCmd{t.player_id, std::move(buf)});
    }
}

void MatchmakerQueue::NotifyTimeout(const MatchmakerTicket& ticket) {
    auto entry = registry_.Find(ticket.player_id);
    if (!entry) return;

    // Binary notification: [ticket_id:u64]
    // Wrapped in 4-byte PacketHeader (size + id=0x3003 kQueueStatus)
    uint16_t total_size = 4 + 8;
    uint16_t packet_id = 0x3003; // kQueueStatus

    auto buf_result = pool_.Allocate(total_size);
    if (!buf_result) return;
    auto& buf = *buf_result;

    auto writable = buf->Writable();
    uint8_t* p = reinterpret_cast<uint8_t*>(writable.data());

    std::memcpy(p, &total_size, 2); p += 2;
    std::memcpy(p, &packet_id, 2); p += 2;
    std::memcpy(p, &ticket.ticket_id, 8);

    buf->Commit(total_size);
    net_dispatcher_(entry->context_id,
                   net::SendToPlayerCmd{ticket.player_id, std::move(buf)});
}

QueryParser::Properties MatchmakerQueue::TicketToProps(const MatchmakerTicket& t) const {
    QueryParser::Properties props;
    for (auto& [k, v] : t.string_props) {
        props.string_keys.push_back(k);
        props.string_values.push_back(v);
    }
    for (auto& [k, v] : t.numeric_props) {
        props.numeric_keys.push_back(k);
        props.numeric_values.push_back(v);
    }
    return props;
}

} // namespace servergame::matchmaker
