#include <gtest/gtest.h>
#include "server/game_match.h"
#include <shared/game_types.h>
#include <shared/byte_buffer.h>

using namespace voxelarena;

TEST(GameMatch, ConstructsWithoutCrash) {
    GameMatch match;
    EXPECT_TRUE(true);
}

TEST(GameMatch, InputCmdSerializationRoundtrip) {
    InputCmd cmd;
    cmd.sequence = 42;
    cmd.moveBits = MOVE_FORWARD | MOVE_JUMP;
    cmd.yaw = 1.5f;
    cmd.pitch = -0.3f;
    cmd.dt = 1.0f / 60.0f;

    ByteWriter w;
    writeInputCmd(w, cmd);

    ByteReader r(w.data(), w.size());
    InputCmd out = readInputCmd(r);

    EXPECT_EQ(out.sequence, 42u);
    EXPECT_EQ(out.moveBits, MOVE_FORWARD | MOVE_JUMP);
    EXPECT_FLOAT_EQ(out.yaw, 1.5f);
    EXPECT_FLOAT_EQ(out.pitch, -0.3f);
}

TEST(GameMatch, PlayerStateSerialization) {
    PlayerState ps;
    ps.playerId = 3;
    ps.position = glm::vec3(10, 20, 30);
    ps.velocity = glm::vec3(1, -2, 0.5f);
    ps.yaw = 2.0f;
    ps.pitch = -1.0f;
    ps.health = 75;
    ps.lastProcessedInput = 100;

    ByteWriter w;
    writePlayerState(w, ps);

    ByteReader r(w.data(), w.size());
    PlayerState out = readPlayerState(r);

    EXPECT_EQ(out.playerId, 3);
    EXPECT_FLOAT_EQ(out.position.x, 10.0f);
    EXPECT_FLOAT_EQ(out.position.y, 20.0f);
    EXPECT_FLOAT_EQ(out.position.z, 30.0f);
    EXPECT_FLOAT_EQ(out.velocity.x, 1.0f);
    EXPECT_FLOAT_EQ(out.velocity.y, -2.0f);
    EXPECT_FLOAT_EQ(out.velocity.z, 0.5f);
    EXPECT_FLOAT_EQ(out.yaw, 2.0f);
    EXPECT_FLOAT_EQ(out.pitch, -1.0f);
    EXPECT_EQ(out.health, 75);
    EXPECT_EQ(out.lastProcessedInput, 100u);
}

TEST(GameMatch, GameSnapshotLayout) {
    GameSnapshot snap;
    EXPECT_EQ(snap.serverTick, 0u);
    EXPECT_EQ(snap.playerCount, 0);
}
