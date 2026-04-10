#include <gtest/gtest.h>
#include "shared/svo_types.h"

using namespace voxelarena;

TEST(SvoConstants, BrickDimension) {
    EXPECT_EQ(BRICK_DIM, 8);
    EXPECT_EQ(BRICK_VOXEL_COUNT, 512);
}

TEST(SvoConstants, InvalidIndex) {
    EXPECT_EQ(INVALID_INDEX, 0xFFFFFFFF);
}

TEST(SvoConstants, MaxDepthAndRootSize) {
    EXPECT_EQ(SVO_MAX_DEPTH, 8);
    EXPECT_FLOAT_EQ(ROOT_HALF_SIZE, 128.0f);
}

TEST(BrickData, SizeIs1024Bytes) {
    static_assert(sizeof(BrickData) == 1024);
}

TEST(SvoNode, DefaultValues) {
    SvoNode node{};
    EXPECT_EQ(node.childMask, 0);
    EXPECT_EQ(node.depth, 0);
    EXPECT_EQ(node.flags, 0);
    EXPECT_EQ(node.childrenStart, 0);
    EXPECT_EQ(node.brickIndex, INVALID_INDEX);
}

TEST(SvoNodeFlags, BitValues) {
    EXPECT_EQ(static_cast<uint16_t>(SvoNodeFlags::IS_LEAF), 1);
    EXPECT_EQ(static_cast<uint16_t>(SvoNodeFlags::HAS_BRICK), 2);
    uint16_t combined = static_cast<uint16_t>(SvoNodeFlags::IS_LEAF)
                      | static_cast<uint16_t>(SvoNodeFlags::HAS_BRICK);
    EXPECT_EQ(combined, 3);
}

TEST(SvoNode, SerializeDeserialize) {
    SvoNode node{};
    node.childMask = 0b10101010;
    node.depth = 5;
    node.flags = static_cast<uint16_t>(SvoNodeFlags::HAS_BRICK);
    node.childrenStart = 42;
    node.brickIndex = 100;
    node.version = 7;
    node.center = glm::vec3(1.0f, 2.0f, 3.0f);
    node.halfSize = 16.0f;

    ByteWriter w;
    writeSvoNode(w, node);

    ByteReader r(w.data(), w.size());
    SvoNode out = readSvoNode(r);

    EXPECT_EQ(out.childMask, node.childMask);
    EXPECT_EQ(out.depth, node.depth);
    EXPECT_EQ(out.flags, node.flags);
    EXPECT_EQ(out.childrenStart, node.childrenStart);
    EXPECT_EQ(out.brickIndex, node.brickIndex);
    EXPECT_EQ(out.version, node.version);
    EXPECT_FLOAT_EQ(out.center.x, 1.0f);
    EXPECT_FLOAT_EQ(out.center.y, 2.0f);
    EXPECT_FLOAT_EQ(out.center.z, 3.0f);
    EXPECT_FLOAT_EQ(out.halfSize, 16.0f);
}
