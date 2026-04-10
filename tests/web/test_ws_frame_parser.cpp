#include <serverweb/WsFrameParser.h>
#include <gtest/gtest.h>
#include <cstring>
#include <string>

using namespace serverweb::ws;

class WsFrameParserTest : public ::testing::Test {
protected:
    WsFrameParser parser_;
    std::vector<std::pair<WsOpcode, std::vector<std::uint8_t>>> frames_;
    std::string last_error_;

    void SetUp() override {
        parser_.SetOnFrame([this](WsOpcode op, std::vector<std::uint8_t> payload) {
            frames_.push_back({op, std::move(payload)});
        });
        parser_.SetOnError([this](std::string_view reason) {
            last_error_ = std::string(reason);
        });
    }

    static std::vector<char> MakeFrame(WsOpcode opcode, std::string_view payload,
                                        bool fin = true, bool masked = false) {
        std::vector<char> frame;
        std::uint8_t b0 = static_cast<std::uint8_t>(opcode);
        if (fin) b0 |= 0x80;
        frame.push_back(static_cast<char>(b0));

        std::uint8_t b1 = masked ? 0x80 : 0x00;
        if (payload.size() < 126) {
            b1 |= static_cast<std::uint8_t>(payload.size());
            frame.push_back(static_cast<char>(b1));
        } else if (payload.size() <= 65535) {
            b1 |= 126;
            frame.push_back(static_cast<char>(b1));
            frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
            frame.push_back(static_cast<char>(payload.size() & 0xFF));
        }

        if (masked) {
            std::uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
            frame.insert(frame.end(), mask, mask + 4);
            for (std::size_t i = 0; i < payload.size(); ++i)
                frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
        } else {
            frame.insert(frame.end(), payload.begin(), payload.end());
        }
        return frame;
    }
};

TEST_F(WsFrameParserTest, SimpleTextFrame) {
    auto frame = MakeFrame(WsOpcode::kText, "hello");
    parser_.Feed(frame.data(), frame.size());
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_EQ(frames_[0].first, WsOpcode::kText);
    std::string msg(frames_[0].second.begin(), frames_[0].second.end());
    EXPECT_EQ(msg, "hello");
}

TEST_F(WsFrameParserTest, MaskedFrame) {
    auto frame = MakeFrame(WsOpcode::kText, "hello", true, true);
    parser_.Feed(frame.data(), frame.size());
    ASSERT_EQ(frames_.size(), 1u);
    std::string msg(frames_[0].second.begin(), frames_[0].second.end());
    EXPECT_EQ(msg, "hello");
}

TEST_F(WsFrameParserTest, EmptyFrame) {
    auto frame = MakeFrame(WsOpcode::kText, "");
    parser_.Feed(frame.data(), frame.size());
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_TRUE(frames_[0].second.empty());
}

TEST_F(WsFrameParserTest, PingFrame) {
    auto frame = MakeFrame(WsOpcode::kPing, "");
    parser_.Feed(frame.data(), frame.size());
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_EQ(frames_[0].first, WsOpcode::kPing);
}

TEST_F(WsFrameParserTest, CloseFrame) {
    std::vector<char> payload = {0x03, static_cast<char>(0xE8)};
    auto frame = MakeFrame(WsOpcode::kClose,
                            std::string_view(payload.data(), payload.size()));
    parser_.Feed(frame.data(), frame.size());
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_EQ(frames_[0].first, WsOpcode::kClose);
}

TEST_F(WsFrameParserTest, FragmentedMessage) {
    auto f1 = MakeFrame(WsOpcode::kText, "hel", false);
    auto f2 = MakeFrame(WsOpcode::kContinuation, "lo ", false);
    auto f3 = MakeFrame(WsOpcode::kContinuation, "world", true);

    parser_.Feed(f1.data(), f1.size());
    EXPECT_EQ(frames_.size(), 0u);
    parser_.Feed(f2.data(), f2.size());
    EXPECT_EQ(frames_.size(), 0u);
    parser_.Feed(f3.data(), f3.size());
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_EQ(frames_[0].first, WsOpcode::kText);
    std::string msg(frames_[0].second.begin(), frames_[0].second.end());
    EXPECT_EQ(msg, "hello world");
}

TEST_F(WsFrameParserTest, MultipleFramesInOneBuffer) {
    auto f1 = MakeFrame(WsOpcode::kText, "aaa");
    auto f2 = MakeFrame(WsOpcode::kText, "bbb");
    f1.insert(f1.end(), f2.begin(), f2.end());
    parser_.Feed(f1.data(), f1.size());
    ASSERT_EQ(frames_.size(), 2u);
}

TEST_F(WsFrameParserTest, IncrementalFeed) {
    auto frame = MakeFrame(WsOpcode::kText, "hello");
    for (std::size_t i = 0; i < frame.size(); ++i) {
        parser_.Feed(&frame[i], 1);
    }
    ASSERT_EQ(frames_.size(), 1u);
    std::string msg(frames_[0].second.begin(), frames_[0].second.end());
    EXPECT_EQ(msg, "hello");
}
