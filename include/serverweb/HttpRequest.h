#pragma once

#include <serverweb/HttpMethod.h>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace serverweb {

class HttpRequest {
public:
    struct Header {
        std::string name;
        std::string value;
    };

    HttpMethod method = HttpMethod::kUnknown;
    std::string path;
    std::string query;
    std::string body;
    bool keep_alive = true;
    std::string authenticated_user_id;

    std::string_view GetHeader(std::string_view name) const {
        for (auto& h : headers_) {
            if (CaseInsensitiveEqual(h.name, name))
                return h.value;
        }
        return {};
    }

    bool HasHeader(std::string_view name) const {
        for (auto& h : headers_) {
            if (CaseInsensitiveEqual(h.name, name))
                return true;
        }
        return false;
    }

    std::string_view ContentType() const {
        return GetHeader("Content-Type");
    }

    std::uint64_t ContentLength() const {
        auto val = GetHeader("Content-Length");
        if (val.empty()) return 0;
        std::uint64_t result = 0;
        std::from_chars(val.data(), val.data() + val.size(), result);
        return result;
    }

    // Lazy query parameter lookup: "key=value&key2=value2"
    std::string_view QueryParam(std::string_view name) const {
        if (!query_parsed_) ParseQueryParams();
        for (auto& [k, v] : query_params_)
            if (k == name) return v;
        return {};
    }

    void AddHeader(std::string name, std::string value) {
        headers_.push_back({std::move(name), std::move(value)});
    }

    const std::vector<Header>& headers() const { return headers_; }

    // Route parameter access (set by Router after RadixTree match)
    std::string_view Param(std::string_view name) const {
        for (auto& [k, v] : params_)
            if (k == name) return v;
        return {};
    }

    bool HasParam(std::string_view name) const {
        for (auto& [k, v] : params_)
            if (k == name) return true;
        return false;
    }

    void SetParams(std::vector<std::pair<std::string_view, std::string_view>> params) {
        params_ = std::move(params);
    }

    void ClearParams() { params_.clear(); }

    void Clear() {
        method = HttpMethod::kUnknown;
        path.clear();
        query.clear();
        body.clear();
        headers_.clear();
        keep_alive = true;
        query_parsed_ = false;
        query_params_.clear();
        params_.clear();
        authenticated_user_id.clear();
    }

private:
    static bool CaseInsensitiveEqual(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (ToLower(a[i]) != ToLower(b[i]))
                return false;
        }
        return true;
    }

    static char ToLower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }

    void ParseQueryParams() const {
        query_parsed_ = true;
        if (query.empty()) return;
        std::string_view q = query;
        while (!q.empty()) {
            auto amp = q.find('&');
            auto pair = q.substr(0, amp);
            auto eq = pair.find('=');
            if (eq != std::string_view::npos) {
                query_params_.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
            } else {
                query_params_.emplace_back(pair, std::string_view{});
            }
            if (amp == std::string_view::npos) break;
            q = q.substr(amp + 1);
        }
    }

    std::vector<Header> headers_;
    std::vector<std::pair<std::string_view, std::string_view>> params_;
    mutable bool query_parsed_ = false;
    mutable std::vector<std::pair<std::string_view, std::string_view>> query_params_;
};

} // namespace serverweb
