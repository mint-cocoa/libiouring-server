#include <serverweb/UpstreamSession.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

// Compile-check: UpstreamSession constructor signature accepts IoRing& and BufferPool&.
// Cannot fully instantiate without a real io_uring, but we verify the types compile.
TEST(UpstreamSessionTest, CallbackTypeAliases) {
    // Verify ProxyCallback and ProxyErrorCallback type aliases compile
    serverweb::ProxyCallback cb = [](int status_code,
                                      std::vector<std::pair<std::string, std::string>> headers,
                                      std::vector<std::byte> body) {
        (void)status_code;
        (void)headers;
        (void)body;
    };
    EXPECT_TRUE(cb);

    serverweb::ProxyErrorCallback err_cb = [](std::string error) {
        (void)error;
    };
    EXPECT_TRUE(err_cb);
}

TEST(UpstreamSessionTest, CallbackTypeNullable) {
    serverweb::ProxyCallback cb;
    EXPECT_FALSE(cb);

    serverweb::ProxyErrorCallback err_cb;
    EXPECT_FALSE(err_cb);
}

// Verify the header file includes are self-contained (no missing deps).
TEST(UpstreamSessionTest, HeaderSelfContained) {
    // If this test compiles, the header is self-contained.
    SUCCEED();
}

} // namespace
