#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace servergame::matchmaker {

class QueryParser {
public:
    enum class Op { kEq, kNeq, kGte, kGt, kLte, kLt };

    struct Condition {
        std::string key;
        Op op;
        std::variant<std::string, double> value;
    };

    /// Lightweight property bag for evaluation (avoids coupling to MatchmakerTicket).
    struct Properties {
        std::vector<std::string> string_keys;
        std::vector<std::string> string_values;
        std::vector<std::string> numeric_keys;
        std::vector<double> numeric_values;
    };

    /// Parse "+key:value +key:>=num -key:value" -> conditions.
    static std::vector<Condition> Parse(std::string_view query);

    /// Check if properties satisfy all conditions.
    static bool Evaluate(const std::vector<Condition>& conditions,
                         const Properties& props);
};

} // namespace servergame::matchmaker
