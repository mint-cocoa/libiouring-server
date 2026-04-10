#pragma once

#include <servercore/io/Session.h>
#include <servercore/buffer/RecvBuffer.h>
#include <servercore/buffer/SendBuffer.h>
#include <cstring>

class PacketSession : public servercore::io::Session {
public:
    using Session::Session;

protected:
    void OnRecv(std::span<const std::byte> data) override;
    virtual void OnPacket(std::uint16_t msg_id, const std::byte* data, std::uint32_t len) = 0;

private:
    servercore::buffer::RecvBuffer recv_buf_;
    static constexpr std::uint32_t kHeaderSize = 4;
    static constexpr std::uint32_t kMaxPacket  = 8192;

    static std::uint16_t ReadLE16(const std::byte* p) {
        std::uint16_t val;
        std::memcpy(&val, p, 2);
        return val;
    }
};
