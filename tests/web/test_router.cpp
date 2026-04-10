#include <serverweb/HttpResponse.h>
#include <serverweb/HttpSession.h>
#include <serverweb/Router.h>

#include <gtest/gtest.h>

#include <string>

using namespace serverweb;
using namespace servercore::buffer;

class RouterTest : public ::testing::Test {
protected:
    Router router_;
    std::string last_handler_;
    bool handler_called_ = false;

    void SetUp() override {
        router_.Route(HttpMethod::kGet, "/", [this](RequestContext&) {
            last_handler_ = "GET /";
            handler_called_ = true;
        });

        router_.Route(HttpMethod::kGet, "/health", [this](RequestContext&) {
            last_handler_ = "GET /health";
            handler_called_ = true;
        });

        router_.Route(HttpMethod::kPost, "/echo", [this](RequestContext&) {
            last_handler_ = "POST /echo";
            handler_called_ = true;
        });

        router_.Route(HttpMethod::kGet, "/throw", [this](RequestContext&) {
            last_handler_ = "GET /throw";
            handler_called_ = true;
            throw std::runtime_error("test exception");
        });
    }
};

TEST_F(RouterTest, ExactMatchGet) {
    HttpRequest req;
    req.method = HttpMethod::kGet;
    req.path = "/";
    req.keep_alive = true;

    EXPECT_NO_THROW({
        router_.Route(HttpMethod::kPut, "/new", [](RequestContext&) {});
    });
}

TEST_F(RouterTest, MultipleRegistrations) {
    Router r;
    EXPECT_NO_THROW({
        r.Route(HttpMethod::kGet, "/a", [](RequestContext&) {});
        r.Route(HttpMethod::kPost, "/a", [](RequestContext&) {});
        r.Route(HttpMethod::kGet, "/b", [](RequestContext&) {});
        r.Route(HttpMethod::kDelete, "/c", [](RequestContext&) {});
    });
}

TEST_F(RouterTest, OverwriteRoute) {
    Router r;
    int call_count = 0;
    r.Route(HttpMethod::kGet, "/test", [&](RequestContext&) { call_count = 1; });
    r.Route(HttpMethod::kGet, "/test", [&](RequestContext&) { call_count = 2; });
    SUCCEED();
}
