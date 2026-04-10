#include <gtest/gtest.h>
#include "shared/protocol.h"

using namespace voxelarena;

TEST(PacketHeader, SizeIs4Bytes) {
    static_assert(sizeof(PacketHeader) == 4);
}

TEST(PacketId, GameChannelClassification) {
    EXPECT_TRUE(isGameChannelPacket(PacketId::C_INPUT_CMD));
    EXPECT_TRUE(isGameChannelPacket(PacketId::C_SESSION_TOKEN));
    EXPECT_TRUE(isGameChannelPacket(PacketId::S_GAME_SNAP));
    EXPECT_TRUE(isGameChannelPacket(PacketId::S_VOXEL_DELTA));
    EXPECT_TRUE(isGameChannelPacket(PacketId::S_SESSION_ACCEPT));
    EXPECT_TRUE(isGameChannelPacket(PacketId::S_PRELOAD_HINT));
}

TEST(PacketId, AssetChannelClassification) {
    EXPECT_FALSE(isGameChannelPacket(PacketId::C_NODE_REQUEST));
    EXPECT_FALSE(isGameChannelPacket(PacketId::S_WORLD_INIT));
    EXPECT_FALSE(isGameChannelPacket(PacketId::S_NODE_RESPONSE));
}

TEST(PacketHeader, SerializeDeserialize) {
    PacketHeader h{512, PacketId::S_GAME_SNAP};
    ByteWriter w;
    writePacketHeader(w, h);
    ASSERT_EQ(w.size(), 4u);

    ByteReader r(w.data(), w.size());
    PacketHeader out = readPacketHeader(r);
    EXPECT_EQ(out.size, 512);
    EXPECT_EQ(out.id, PacketId::S_GAME_SNAP);
}
