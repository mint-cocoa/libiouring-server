#include <serverweb/JwtCodec.h>
#include <gtest/gtest.h>
#include <chrono>

using namespace serverweb::auth;
using namespace serverweb;

class JwtCodecTest : public ::testing::Test {
protected:
    JwtCodec codec_{"test-secret-key-for-unit-tests"};

    JwtClaims MakeClaims(int ttl_seconds = 3600) {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return JwtClaims{
            .sub = "user-123",
            .device_id = "device-abc",
            .iat = now,
            .exp = now + ttl_seconds
        };
    }
};

TEST_F(JwtCodecTest, EncodeDecodeRoundTrip) {
    auto claims = MakeClaims();
    auto token = codec_.Encode(claims);
    EXPECT_FALSE(token.empty());

    auto result = codec_.Decode(token);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().sub, "user-123");
    EXPECT_EQ(result.value().device_id, "device-abc");
    EXPECT_EQ(result.value().iat, claims.iat);
    EXPECT_EQ(result.value().exp, claims.exp);
}

TEST_F(JwtCodecTest, InvalidSignatureRejected) {
    auto token = codec_.Encode(MakeClaims());
    auto tampered = token.substr(0, token.size() - 2) + "XX";
    auto result = codec_.Decode(tampered);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WebError::kUnauthorized);
}

TEST_F(JwtCodecTest, ExpiredTokenRejected) {
    auto claims = MakeClaims(-10);
    auto token = codec_.Encode(claims);
    auto result = codec_.Decode(token);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WebError::kUnauthorized);
}

TEST_F(JwtCodecTest, WrongSecretRejected) {
    auto token = codec_.Encode(MakeClaims());
    JwtCodec other("wrong-secret");
    auto result = other.Decode(token);
    EXPECT_FALSE(result.has_value());
}

TEST_F(JwtCodecTest, MalformedTokenRejected) {
    EXPECT_FALSE(codec_.Decode("not-a-jwt").has_value());
    EXPECT_FALSE(codec_.Decode("a.b").has_value());
    EXPECT_FALSE(codec_.Decode("a.b.c.d").has_value());
    EXPECT_FALSE(codec_.Decode("").has_value());
}
