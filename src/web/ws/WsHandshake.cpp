#include <serverweb/WsHandshake.h>
#include <serverweb/HttpRequest.h>
#include <openssl/evp.h>
#include <cstring>

namespace serverweb::ws {

static constexpr std::string_view kMagicGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static bool CaseInsensitiveContains(std::string_view haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) return false;
    for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool WsHandshake::ValidateUpgrade(const HttpRequest& request) {
    auto upgrade = request.GetHeader("Upgrade");
    if (!CaseInsensitiveContains(upgrade, "websocket")) return false;

    auto connection = request.GetHeader("Connection");
    if (!CaseInsensitiveContains(connection, "upgrade")) return false;

    auto version = request.GetHeader("Sec-WebSocket-Version");
    if (version != "13") return false;

    auto key = request.GetHeader("Sec-WebSocket-Key");
    return !key.empty();
}

std::string WsHandshake::ComputeAcceptKey(std::string_view client_key) {
    std::string input(client_key);
    input.append(kMagicGuid);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_Digest(input.data(), input.size(), hash, &hash_len, EVP_sha1(), nullptr);

    // Standard base64 encode (NOT url-safe -- WebSocket uses regular base64)
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    for (unsigned int i = 0; i < hash_len; i += 3) {
        std::uint32_t n = static_cast<std::uint32_t>(hash[i]) << 16;
        if (i + 1 < hash_len) n |= static_cast<std::uint32_t>(hash[i + 1]) << 8;
        if (i + 2 < hash_len) n |= static_cast<std::uint32_t>(hash[i + 2]);

        result.push_back(table[(n >> 18) & 0x3F]);
        result.push_back(table[(n >> 12) & 0x3F]);
        result.push_back(i + 1 < hash_len ? table[(n >> 6) & 0x3F] : '=');
        result.push_back(i + 2 < hash_len ? table[n & 0x3F] : '=');
    }
    return result;
}

servercore::buffer::SendBufferRef WsHandshake::BuildUpgradeResponse(
    std::string_view accept_key, servercore::buffer::BufferPool& pool) {
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";
    response.append(accept_key);
    response.append("\r\n\r\n");

    auto result = pool.Allocate(static_cast<std::uint32_t>(response.size()));
    if (!result) return nullptr;
    auto buf = std::move(*result);

    std::memcpy(buf->Writable().data(), response.data(), response.size());
    buf->Commit(static_cast<std::uint32_t>(response.size()));
    return buf;
}

} // namespace serverweb::ws
