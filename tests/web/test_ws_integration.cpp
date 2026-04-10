#include <serverweb/WebServer.h>
#include <serverweb/WebSocketHandler.h>
#include <serverweb/HttpSession.h>

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

using namespace serverweb;

class EchoHandler : public ws::WebSocketHandler {
public:
    void OnMessage(HttpSession& session,
                   std::string_view data, bool) override {
        session.SendText(data);
    }
};

class WsIntegrationTest : public ::testing::Test {
protected:
    static constexpr uint16_t kPort = 19876;

    std::unique_ptr<WebServer> server_;

    void SetUp() override {
        WebServerConfig config;
        config.port = kPort;
        config.worker_count = 1;
        config.io_timeout = std::chrono::milliseconds{1};
        server_ = std::make_unique<WebServer>(config);
        server_->WebSocket("/ws/echo", std::make_shared<EchoHandler>());
        server_->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }

    void TearDown() override {
        if (server_) server_->Stop();
    }

    int ConnectTcp() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    bool SendAll(int fd, const void* data, size_t len) {
        auto* p = static_cast<const char*>(data);
        while (len > 0) {
            auto n = ::send(fd, p, len, 0);
            if (n <= 0) return false;
            p += n;
            len -= n;
        }
        return true;
    }

    std::string RecvAll(int fd, size_t max_len = 4096, int timeout_ms = 1000) {
        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string result;
        char buf[4096];
        auto n = ::recv(fd, buf, std::min(max_len, sizeof(buf)), 0);
        if (n > 0) result.assign(buf, n);
        return result;
    }

    // Build a masked text frame (client -> server must be masked per RFC 6455)
    std::vector<uint8_t> MakeMaskedTextFrame(std::string_view msg) {
        std::vector<uint8_t> frame;
        frame.push_back(0x81);  // FIN + Text opcode

        uint8_t mask_bit = 0x80;
        if (msg.size() < 126) {
            frame.push_back(mask_bit | static_cast<uint8_t>(msg.size()));
        } else if (msg.size() <= 65535) {
            frame.push_back(mask_bit | 126);
            frame.push_back(static_cast<uint8_t>((msg.size() >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(msg.size() & 0xFF));
        }

        // Masking key
        uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
        frame.insert(frame.end(), mask, mask + 4);

        // Masked payload
        for (size_t i = 0; i < msg.size(); ++i)
            frame.push_back(static_cast<uint8_t>(msg[i]) ^ mask[i % 4]);

        return frame;
    }
};

TEST_F(WsIntegrationTest, UpgradeAndEcho) {
    int fd = ConnectTcp();
    ASSERT_GE(fd, 0);

    // Send WebSocket upgrade request
    std::string upgrade =
        "GET /ws/echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    ASSERT_TRUE(SendAll(fd, upgrade.data(), upgrade.size()));

    auto response = RecvAll(fd);
    EXPECT_NE(response.find("101 Switching Protocols"), std::string::npos);
    EXPECT_NE(response.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), std::string::npos);

    // Send masked text frame "hello"
    auto frame = MakeMaskedTextFrame("hello");
    ASSERT_TRUE(SendAll(fd, frame.data(), frame.size()));

    // Receive echo (unmasked server frame)
    auto echo = RecvAll(fd);
    ASSERT_GE(echo.size(), 7u);  // 2 header + 5 payload

    EXPECT_EQ(static_cast<uint8_t>(echo[0]), 0x81u);  // FIN + Text
    uint8_t len = static_cast<uint8_t>(echo[1]) & 0x7F;
    EXPECT_EQ(len, 5u);
    std::string echoed(echo.data() + 2, len);
    EXPECT_EQ(echoed, "hello");

    close(fd);
}

TEST_F(WsIntegrationTest, NonUpgradeGetsBadRequest) {
    int fd = ConnectTcp();
    ASSERT_GE(fd, 0);

    // Normal GET without upgrade headers
    std::string request =
        "GET /ws/echo HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";

    ASSERT_TRUE(SendAll(fd, request.data(), request.size()));

    auto response = RecvAll(fd);
    EXPECT_NE(response.find("400"), std::string::npos);

    close(fd);
}
