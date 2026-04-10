#include <serverweb/JwtCodec.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <algorithm>
#include <chrono>

namespace serverweb::auth {

JwtCodec::JwtCodec(std::string_view secret) : secret_(secret) {}

std::string JwtCodec::Base64UrlEncode(std::span<const std::uint8_t> data) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(4 * ((data.size() + 2) / 3));

    for (std::size_t i = 0; i < data.size(); i += 3) {
        std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<std::uint32_t>(data[i + 2]);

        result.push_back(table[(n >> 18) & 0x3F]);
        result.push_back(table[(n >> 12) & 0x3F]);
        if (i + 1 < data.size()) result.push_back(table[(n >> 6) & 0x3F]);
        if (i + 2 < data.size()) result.push_back(table[n & 0x3F]);
    }

    std::replace(result.begin(), result.end(), '+', '-');
    std::replace(result.begin(), result.end(), '/', '_');
    return result;
}

std::string JwtCodec::Base64UrlEncode(std::string_view data) {
    return Base64UrlEncode(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

std::vector<std::uint8_t> JwtCodec::Base64UrlDecode(std::string_view encoded) {
    std::string b64(encoded);
    std::replace(b64.begin(), b64.end(), '-', '+');
    std::replace(b64.begin(), b64.end(), '_', '/');
    while (b64.size() % 4 != 0) b64.push_back('=');

    static constexpr int dt[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };

    std::vector<std::uint8_t> result;
    result.reserve(b64.size() * 3 / 4);

    for (std::size_t i = 0; i < b64.size(); i += 4) {
        std::uint32_t n = 0;
        int pad = 0;
        for (int j = 0; j < 4; ++j) {
            auto c = static_cast<unsigned char>(b64[i + j]);
            if (c == '=') { ++pad; continue; }
            if (c >= 128 || dt[c] < 0) return {};
            n = (n << 6) | static_cast<std::uint32_t>(dt[c]);
        }
        n <<= (pad * 6);
        result.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFF));
        if (pad < 2) result.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        if (pad < 1) result.push_back(static_cast<std::uint8_t>(n & 0xFF));
    }
    return result;
}

std::string JwtCodec::Sign(std::string_view data) const {
    std::uint8_t digest[EVP_MAX_MD_SIZE];
    std::size_t len = 0;

    auto* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    auto* ctx = EVP_MAC_CTX_new(mac);
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
                                          const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };
    EVP_MAC_init(ctx,
                 reinterpret_cast<const unsigned char*>(secret_.data()),
                 secret_.size(), params);
    EVP_MAC_update(ctx,
                   reinterpret_cast<const unsigned char*>(data.data()),
                   data.size());
    EVP_MAC_final(ctx, digest, &len, sizeof(digest));
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);

    return Base64UrlEncode(std::span<const std::uint8_t>(digest, len));
}

std::string JwtCodec::Encode(const JwtClaims& claims) const {
    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    nlohmann::json payload;
    payload["sub"] = claims.sub;
    if (!claims.device_id.empty()) payload["did"] = claims.device_id;
    payload["iat"] = claims.iat;
    payload["exp"] = claims.exp;

    auto h = Base64UrlEncode(header.dump());
    auto p = Base64UrlEncode(payload.dump());
    auto header_payload = h + "." + p;
    return header_payload + "." + Sign(header_payload);
}

std::expected<JwtClaims, WebError> JwtCodec::Decode(std::string_view token) const {
    auto dot1 = token.find('.');
    if (dot1 == std::string_view::npos)
        return std::unexpected(WebError::kBadRequest);
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos)
        return std::unexpected(WebError::kBadRequest);
    if (token.find('.', dot2 + 1) != std::string_view::npos)
        return std::unexpected(WebError::kBadRequest);

    auto header_payload = token.substr(0, dot2);
    auto signature = token.substr(dot2 + 1);

    if (Sign(header_payload) != signature)
        return std::unexpected(WebError::kUnauthorized);

    auto payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    auto payload_bytes = Base64UrlDecode(payload_b64);
    if (payload_bytes.empty())
        return std::unexpected(WebError::kBadRequest);

    std::string payload_str(payload_bytes.begin(), payload_bytes.end());
    auto json = nlohmann::json::parse(payload_str, nullptr, false);
    if (json.is_discarded())
        return std::unexpected(WebError::kBadRequest);

    JwtClaims claims;
    if (json.contains("sub")) claims.sub = json["sub"].get<std::string>();
    if (json.contains("did")) claims.device_id = json["did"].get<std::string>();
    if (json.contains("iat")) claims.iat = json["iat"].get<std::int64_t>();
    if (json.contains("exp")) claims.exp = json["exp"].get<std::int64_t>();

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (claims.exp > 0 && claims.exp < now)
        return std::unexpected(WebError::kUnauthorized);

    return claims;
}

} // namespace serverweb::auth
