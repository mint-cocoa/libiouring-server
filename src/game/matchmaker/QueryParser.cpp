#include <servergame/QueryParser.h>

#include <charconv>
#include <cstdlib>

namespace servergame::matchmaker {

std::vector<QueryParser::Condition> QueryParser::Parse(std::string_view query) {
    std::vector<Condition> result;

    std::size_t pos = 0;
    while (pos < query.size()) {
        // Skip whitespace
        while (pos < query.size() && query[pos] == ' ') ++pos;
        if (pos >= query.size()) break;

        // Read prefix (+/-)
        bool negate = false;
        if (query[pos] == '+') { ++pos; }
        else if (query[pos] == '-') { negate = true; ++pos; }
        else { ++pos; continue; } // skip unknown

        // Read key (until ':')
        auto colon = query.find(':', pos);
        if (colon == std::string_view::npos) break;
        std::string key(query.substr(pos, colon - pos));
        pos = colon + 1;

        // Read value (until space or end)
        auto end = query.find(' ', pos);
        if (end == std::string_view::npos) end = query.size();
        std::string_view val_str = query.substr(pos, end - pos);
        pos = end;

        if (negate) {
            result.push_back({std::move(key), Op::kNeq, std::string(val_str)});
            continue;
        }

        // Parse operator from value
        Op op = Op::kEq;
        if (val_str.starts_with(">=")) { op = Op::kGte; val_str.remove_prefix(2); }
        else if (val_str.starts_with("<=")) { op = Op::kLte; val_str.remove_prefix(2); }
        else if (val_str.starts_with(">"))  { op = Op::kGt;  val_str.remove_prefix(1); }
        else if (val_str.starts_with("<"))  { op = Op::kLt;  val_str.remove_prefix(1); }

        if (op != Op::kEq) {
            // Numeric comparison
            double num = 0;
            auto [ptr, ec] = std::from_chars(val_str.data(), val_str.data() + val_str.size(), num);
            if (ec == std::errc{})
                result.push_back({std::move(key), op, num});
        } else {
            result.push_back({std::move(key), Op::kEq, std::string(val_str)});
        }
    }

    return result;
}

bool QueryParser::Evaluate(const std::vector<Condition>& conditions,
                           const Properties& props) {
    for (auto& cond : conditions) {
        if (std::holds_alternative<double>(cond.value)) {
            // Find numeric property
            double val = 0;
            bool found = false;
            for (std::size_t i = 0; i < props.numeric_keys.size(); ++i) {
                if (props.numeric_keys[i] == cond.key) {
                    val = props.numeric_values[i];
                    found = true;
                    break;
                }
            }
            if (!found) return false;

            switch (cond.op) {
                case Op::kGte: if (!(val >= std::get<double>(cond.value))) return false; break;
                case Op::kGt:  if (!(val >  std::get<double>(cond.value))) return false; break;
                case Op::kLte: if (!(val <= std::get<double>(cond.value))) return false; break;
                case Op::kLt:  if (!(val <  std::get<double>(cond.value))) return false; break;
                default: return false;
            }
        } else {
            // Find string property
            std::string val;
            bool found = false;
            for (std::size_t i = 0; i < props.string_keys.size(); ++i) {
                if (props.string_keys[i] == cond.key) {
                    val = props.string_values[i];
                    found = true;
                    break;
                }
            }

            switch (cond.op) {
                case Op::kEq:
                    if (!found || val != std::get<std::string>(cond.value)) return false;
                    break;
                case Op::kNeq:
                    if (found && val == std::get<std::string>(cond.value)) return false;
                    break;
                default: return false;
            }
        }
    }
    return true;
}

} // namespace servergame::matchmaker
