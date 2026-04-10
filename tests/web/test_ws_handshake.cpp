#include <serverweb/WsHandshake.h>
#include <serverweb/HttpRequest.h>
#include <gtest/gtest.h>

using namespace serverweb;
using namespace serverweb::ws;

TEST(WsHandshakeTest, ComputeAcceptKey) {
    // RFC 6455 Section 4.2.2 example
    auto result = WsHandshake::ComputeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_EQ(result, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WsHandshakeTest, ValidateUpgradeSuccess) {
    HttpRequest req;
    req.method = HttpMethod::kGet;
    req.AddHeader("Upgrade", "websocket");
    req.AddHeader("Connection", "Upgrade");
    req.AddHeader("Sec-WebSocket-Version", "13");
    req.AddHeader("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_TRUE(WsHandshake::ValidateUpgrade(req));
}

TEST(WsHandshakeTest, ValidateUpgradeMissingKey) {
    HttpRequest req;
    req.AddHeader("Upgrade", "websocket");
    req.AddHeader("Connection", "Upgrade");
    req.AddHeader("Sec-WebSocket-Version", "13");
    EXPECT_FALSE(WsHandshake::ValidateUpgrade(req));
}

TEST(WsHandshakeTest, ValidateUpgradeWrongVersion) {
    HttpRequest req;
    req.AddHeader("Upgrade", "websocket");
    req.AddHeader("Connection", "Upgrade");
    req.AddHeader("Sec-WebSocket-Version", "8");
    req.AddHeader("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_FALSE(WsHandshake::ValidateUpgrade(req));
}

TEST(WsHandshakeTest, BuildUpgradeResponse) {
    servercore::buffer::BufferPool pool;
    auto buf = WsHandshake::BuildUpgradeResponse("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", pool);
    ASSERT_NE(buf, nullptr);
    auto data = buf->Data();
    std::string response(reinterpret_cast<const char*>(data.data()), data.size());
    EXPECT_NE(response.find("101 Switching Protocols"), std::string::npos);
    EXPECT_NE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
              std::string::npos);
}
