#include <gtest/gtest.h>
#include <serverweb/UpstreamPool.h>

using namespace serverweb;

TEST(UpstreamPoolTest, InitialCountsAreZero) {
    UpstreamPool pool;
    EXPECT_EQ(pool.ActiveCount(), 0);
    EXPECT_EQ(pool.IdleCount("localhost:8000"), 0);
}

TEST(UpstreamTargetTest, KeyFormat) {
    UpstreamTarget target{"gateway", 8000};
    EXPECT_EQ(target.Key(), "gateway:8000");
}
