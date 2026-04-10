#pragma once
#include <serverweb/Error.h>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace serverweb::auth {

struct JwtClaims {
    std::string sub;
    std::string device_id;
    std::int64_t iat = 0;
    std::int64_t exp = 0;
};

class JwtCodec {
public:
    explicit JwtCodec(std::string_view secret);

    std::string Encode(const JwtClaims& claims) const;
    std::expected<JwtClaims, WebError> Decode(std::string_view token) const;

private:
    std::string secret_;

    std::string Sign(std::string_view data) const;
    static std::string Base64UrlEncode(std::span<const std::uint8_t> data);
    static std::string Base64UrlEncode(std::string_view data);
    static std::vector<std::uint8_t> Base64UrlDecode(std::string_view encoded);
};

} // namespace serverweb::auth
