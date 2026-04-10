#include <serverweb/HttpParser.h>
#include <serverweb/HttpRequest.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace serverweb;

class HttpParserTest : public ::testing::Test {
protected:
    HttpParser parser_;
    std::vector<HttpRequest> requests_;

    void SetUp() override {
        parser_.SetOnRequest([this](HttpRequest& req) -> bool {
            // Copy the request since it will be cleared on next message
            HttpRequest copy;
            copy.method = req.method;
            copy.path = req.path;
            copy.query = req.query;
            copy.body = req.body;
            copy.keep_alive = req.keep_alive;
            for (auto& h : req.headers())
                copy.AddHeader(std::string(h.name), std::string(h.value));
            requests_.push_back(std::move(copy));
            return true;
        });
    }

    std::uint32_t Feed(const std::string& data) {
        return parser_.Feed(data.data(), static_cast<std::uint32_t>(data.size()));
    }
};

TEST_F(HttpParserTest, SimpleGet) {
    std::string raw = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].method, HttpMethod::kGet);
    EXPECT_EQ(requests_[0].path, "/");
    EXPECT_TRUE(requests_[0].query.empty());
    EXPECT_TRUE(requests_[0].body.empty());
    EXPECT_TRUE(requests_[0].keep_alive);
    EXPECT_EQ(requests_[0].GetHeader("Host"), "localhost");
}

TEST_F(HttpParserTest, QueryString) {
    std::string raw = "GET /search?q=hello&page=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].path, "/search");
    EXPECT_EQ(requests_[0].query, "q=hello&page=1");
}

TEST_F(HttpParserTest, PostWithBody) {
    std::string raw =
        "POST /api HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"key\":\"val\"}";

    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].method, HttpMethod::kPost);
    EXPECT_EQ(requests_[0].path, "/api");
    EXPECT_EQ(requests_[0].body, R"({"key":"val"})");
    EXPECT_EQ(requests_[0].GetHeader("Content-Type"), "application/json");
}

TEST_F(HttpParserTest, ChunkedDelivery) {
    // Simulate data arriving in small chunks
    std::string raw = "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n";

    for (std::size_t i = 0; i < raw.size(); ++i) {
        parser_.Feed(&raw[i], 1);
    }

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].method, HttpMethod::kGet);
    EXPECT_EQ(requests_[0].path, "/test");
}

TEST_F(HttpParserTest, Pipelining) {
    std::string raw =
        "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /third HTTP/1.1\r\nHost: localhost\r\n\r\n";

    Feed(raw);

    ASSERT_EQ(requests_.size(), 3u);
    EXPECT_EQ(requests_[0].path, "/first");
    EXPECT_EQ(requests_[1].path, "/second");
    EXPECT_EQ(requests_[2].path, "/third");
}

TEST_F(HttpParserTest, InvalidRequest) {
    std::string raw = "INVALID GARBAGE\r\n\r\n";
    Feed(raw);

    EXPECT_TRUE(parser_.HasError());
    EXPECT_EQ(requests_.size(), 0u);
}

TEST_F(HttpParserTest, ConnectionClose) {
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_FALSE(requests_[0].keep_alive);
}

TEST_F(HttpParserTest, MultipleHeaders) {
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept: text/html\r\n"
        "Accept-Language: en-US\r\n"
        "User-Agent: TestClient/1.0\r\n"
        "\r\n";

    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].GetHeader("Accept"), "text/html");
    EXPECT_EQ(requests_[0].GetHeader("Accept-Language"), "en-US");
    EXPECT_EQ(requests_[0].GetHeader("User-Agent"), "TestClient/1.0");
}

TEST_F(HttpParserTest, ResetAfterError) {
    std::string bad = "INVALID GARBAGE\r\n\r\n";
    Feed(bad);
    EXPECT_TRUE(parser_.HasError());

    parser_.Reset();
    EXPECT_FALSE(parser_.HasError());

    std::string good = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Feed(good);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].path, "/");
}

TEST_F(HttpParserTest, DeleteMethod) {
    std::string raw = "DELETE /resource/123 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].method, HttpMethod::kDelete);
    EXPECT_EQ(requests_[0].path, "/resource/123");
}

TEST_F(HttpParserTest, HeadMethod) {
    std::string raw = "HEAD / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Feed(raw);

    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].method, HttpMethod::kHead);
}
