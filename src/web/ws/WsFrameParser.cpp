#include <serverweb/WsFrameParser.h>
#include <algorithm>
#include <cstring>

namespace serverweb::ws {

void WsFrameParser::Reset() {
    state_ = State::Header;
    current_ = {};
    payload_received_ = 0;
    error_ = false;
    fragment_opcode_ = WsOpcode::kContinuation;
    fragment_buffer_.clear();
    in_fragment_ = false;
    buffer_.clear();
}

void WsFrameParser::SetError(std::string_view reason) {
    error_ = true;
    if (on_error_) on_error_(reason);
}

void WsFrameParser::Unmask() {
    if (!current_.masked) return;
    for (std::size_t i = 0; i < current_.payload.size(); ++i) {
        current_.payload[i] ^= current_.masking_key[i % 4];
    }
}

std::size_t WsFrameParser::Feed(const char* data, std::size_t len) {
    if (error_) return 0;

    // Append new data to internal buffer
    auto* p = reinterpret_cast<const std::uint8_t*>(data);
    buffer_.insert(buffer_.end(), p, p + len);

    // Parse as much as possible from the buffer
    std::size_t consumed = ParseBuffer();

    // Remove consumed bytes from the front of the buffer
    if (consumed > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
    }

    return len; // We always accept all input into the buffer
}

std::size_t WsFrameParser::ParseBuffer() {
    const std::uint8_t* p = buffer_.data();
    std::size_t len = buffer_.size();
    std::size_t consumed = 0;

    while (consumed < len && !error_) {
        switch (state_) {
        case State::Header: {
            if (consumed + 2 > len) return consumed;
            std::uint8_t b0 = p[consumed++];
            std::uint8_t b1 = p[consumed++];

            current_ = {};
            current_.fin = (b0 & 0x80) != 0;
            current_.opcode = static_cast<WsOpcode>(b0 & 0x0F);
            current_.masked = (b1 & 0x80) != 0;
            std::uint8_t len7 = b1 & 0x7F;

            if (len7 < 126) {
                current_.payload_length = len7;
                if (current_.payload_length == 0) {
                    if (current_.masked) {
                        state_ = State::MaskKey;
                    } else {
                        current_.payload.clear();
                        payload_received_ = 0;
                        ProcessFrame();
                        state_ = State::Header;
                    }
                } else {
                    current_.payload.resize(current_.payload_length);
                    payload_received_ = 0;
                    state_ = current_.masked ? State::MaskKey : State::Payload;
                }
            } else if (len7 == 126) {
                state_ = State::PayloadLen16;
            } else {
                state_ = State::PayloadLen64;
            }
            break;
        }
        case State::PayloadLen16: {
            if (consumed + 2 > len) return consumed;
            current_.payload_length =
                (static_cast<std::uint64_t>(p[consumed]) << 8) |
                 static_cast<std::uint64_t>(p[consumed + 1]);
            consumed += 2;
            current_.payload.resize(current_.payload_length);
            payload_received_ = 0;
            state_ = current_.masked ? State::MaskKey : State::Payload;
            break;
        }
        case State::PayloadLen64: {
            if (consumed + 8 > len) return consumed;
            current_.payload_length = 0;
            for (int i = 0; i < 8; ++i) {
                current_.payload_length =
                    (current_.payload_length << 8) | p[consumed + i];
            }
            consumed += 8;
            if (current_.payload_length > 16 * 1024 * 1024) {
                SetError("payload too large");
                return consumed;
            }
            current_.payload.resize(current_.payload_length);
            payload_received_ = 0;
            state_ = current_.masked ? State::MaskKey : State::Payload;
            break;
        }
        case State::MaskKey: {
            if (consumed + 4 > len) return consumed;
            std::memcpy(current_.masking_key, &p[consumed], 4);
            consumed += 4;
            if (current_.payload_length == 0) {
                current_.payload.clear();
                Unmask();
                ProcessFrame();
                state_ = State::Header;
            } else {
                state_ = State::Payload;
            }
            break;
        }
        case State::Payload: {
            std::size_t remaining = current_.payload_length - payload_received_;
            std::size_t available = len - consumed;
            std::size_t to_copy = std::min(remaining, available);

            std::memcpy(current_.payload.data() + payload_received_,
                        &p[consumed], to_copy);
            consumed += to_copy;
            payload_received_ += to_copy;

            if (payload_received_ == current_.payload_length) {
                Unmask();
                ProcessFrame();
                state_ = State::Header;
            }
            break;
        }
        }
    }
    return consumed;
}

void WsFrameParser::ProcessFrame() {
    auto opcode = current_.opcode;

    if (opcode >= WsOpcode::kClose) {
        if (on_frame_) on_frame_(opcode, std::move(current_.payload));
        return;
    }

    if (opcode != WsOpcode::kContinuation) {
        if (current_.fin) {
            if (on_frame_) on_frame_(opcode, std::move(current_.payload));
        } else {
            in_fragment_ = true;
            fragment_opcode_ = opcode;
            fragment_buffer_ = std::move(current_.payload);
        }
    } else {
        if (!in_fragment_) {
            SetError("unexpected continuation frame");
            return;
        }
        fragment_buffer_.insert(fragment_buffer_.end(),
                                current_.payload.begin(),
                                current_.payload.end());
        if (current_.fin) {
            in_fragment_ = false;
            if (on_frame_) on_frame_(fragment_opcode_, std::move(fragment_buffer_));
            fragment_buffer_.clear();
        }
    }
}

} // namespace serverweb::ws
