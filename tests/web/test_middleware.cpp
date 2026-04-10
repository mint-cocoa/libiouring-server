#include <serverweb/Middleware.h>
#include <serverweb/HttpResponse.h>
#include <serverweb/Router.h>

#include <servercore/buffer/SendBuffer.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace serverweb;
using namespace servercore::buffer;

// Test middleware that records execution order
class OrderMiddleware : public IMiddleware {
public:
    OrderMiddleware(std::vector<std::string>& log, std::string name)
        : log_(log), name_(std::move(name)) {}

    void Process(RequestContext&, NextFn next) override {
        log_.push_back(name_ + ":before");
        next();
        log_.push_back(name_ + ":after");
    }

private:
    std::vector<std::string>& log_;
    std::string name_;
};

// Test middleware that short-circuits (doesn't call next)
class ShortCircuitMiddleware : public IMiddleware {
public:
    ShortCircuitMiddleware(std::vector<std::string>& log, std::string name)
        : log_(log), name_(std::move(name)) {}

    void Process(RequestContext&, NextFn) override {
        log_.push_back(name_ + ":stop");
        // Don't call next() -- short-circuit
    }

private:
    std::vector<std::string>& log_;
    std::string name_;
};

// Helper to create a minimal RequestContext for testing.
// We need actual objects but won't use session I/O.
class MiddlewareTest : public ::testing::Test {
protected:
    std::vector<std::string> log_;
};

TEST_F(MiddlewareTest, EmptyPipeline) {
    Pipeline pipeline;
    EXPECT_TRUE(pipeline.Empty());

    bool called = false;
    // Create minimal context - we won't access session/request/response
    // Use a reference trick with placement
    HttpRequest req;
    HttpResponse resp;
    BufferPool pool;

    // We can't easily create a real RequestContext without a session,
    // so test Pipeline::Execute with a dummy context using reinterpret.
    // For unit tests, the middleware won't actually use the context.
    pipeline.Execute(
        *reinterpret_cast<RequestContext*>(&req),  // dummy, unused
        [&called]() { called = true; }
    );
    EXPECT_TRUE(called);
}

TEST_F(MiddlewareTest, SingleMiddleware) {
    Pipeline pipeline;
    pipeline.Use(std::make_shared<OrderMiddleware>(log_, "A"));
    EXPECT_FALSE(pipeline.Empty());

    bool handler_called = false;
    HttpRequest req;
    pipeline.Execute(
        *reinterpret_cast<RequestContext*>(&req),
        [&]() {
            log_.push_back("handler");
            handler_called = true;
        }
    );

    EXPECT_TRUE(handler_called);
    ASSERT_EQ(log_.size(), 3);
    EXPECT_EQ(log_[0], "A:before");
    EXPECT_EQ(log_[1], "handler");
    EXPECT_EQ(log_[2], "A:after");
}

TEST_F(MiddlewareTest, ChainOrder) {
    Pipeline pipeline;
    pipeline.Use(std::make_shared<OrderMiddleware>(log_, "A"));
    pipeline.Use(std::make_shared<OrderMiddleware>(log_, "B"));
    pipeline.Use(std::make_shared<OrderMiddleware>(log_, "C"));

    HttpRequest req;
    pipeline.Execute(
        *reinterpret_cast<RequestContext*>(&req),
        [&]() { log_.push_back("handler"); }
    );

    // Expected: A:before -> B:before -> C:before -> handler -> C:after -> B:after -> A:after
    ASSERT_EQ(log_.size(), 7);
    EXPECT_EQ(log_[0], "A:before");
    EXPECT_EQ(log_[1], "B:before");
    EXPECT_EQ(log_[2], "C:before");
    EXPECT_EQ(log_[3], "handler");
    EXPECT_EQ(log_[4], "C:after");
    EXPECT_EQ(log_[5], "B:after");
    EXPECT_EQ(log_[6], "A:after");
}

TEST_F(MiddlewareTest, ShortCircuit) {
    Pipeline pipeline;
    pipeline.Use(std::make_shared<OrderMiddleware>(log_, "A"));
    pipeline.Use(std::make_shared<ShortCircuitMiddleware>(log_, "B"));
    pipeline.Use(std::make_shared<OrderMiddleware>(log_, "C"));

    bool handler_called = false;
    HttpRequest req;
    pipeline.Execute(
        *reinterpret_cast<RequestContext*>(&req),
        [&]() {
            log_.push_back("handler");
            handler_called = true;
        }
    );

    // B stops the chain, so C and handler are never reached
    EXPECT_FALSE(handler_called);
    ASSERT_EQ(log_.size(), 3);
    EXPECT_EQ(log_[0], "A:before");
    EXPECT_EQ(log_[1], "B:stop");
    EXPECT_EQ(log_[2], "A:after");
}
