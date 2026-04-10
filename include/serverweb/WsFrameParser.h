#pragma once
#include <serverweb/WsFrame.h>
#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace serverweb::ws {

class WsFrameParser {
public:
    using OnFrameCallback = std::function<void(WsOpcode opcode,
                                                std::vector<std::uint8_t> payload)>;
    using OnErrorCallback = std::function<void(std::string_view reason)>;

    void SetOnFrame(OnFrameCallback cb) { on_frame_ = std::move(cb); }
    void SetOnError(OnErrorCallback cb) { on_error_ = std::move(cb); }

    std::size_t Feed(const char* data, std::size_t len);
    bool HasError() const { return error_; }
    void Reset();

private:
    enum class State { Header, PayloadLen16, PayloadLen64, MaskKey, Payload };

    State state_ = State::Header;
    WsFrame current_;
    std::size_t payload_received_ = 0;
    bool error_ = false;

    WsOpcode fragment_opcode_ = WsOpcode::kContinuation;
    std::vector<std::uint8_t> fragment_buffer_;
    bool in_fragment_ = false;

    std::vector<std::uint8_t> buffer_;

    OnFrameCallback on_frame_;
    OnErrorCallback on_error_;

    void SetError(std::string_view reason);
    void ProcessFrame();
    void Unmask();
    std::size_t ParseBuffer();
};

} // namespace serverweb::ws
