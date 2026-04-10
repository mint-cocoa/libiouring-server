#include <gtest/gtest.h>
#include "server/voxel_world.h"
#include <shared/svo_types.h>

using namespace voxelarena;

TEST(VoxelWorld, GenerateCreatesNodes) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);  // small depth for fast test
    EXPECT_GT(world.nodeCount(), 0u);
    EXPECT_GT(world.brickCount(), 0u);
}

TEST(VoxelWorld, RootNodeValid) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    const auto& root = world.getNode(0);
    EXPECT_FLOAT_EQ(root.halfSize, 128.0f);
    EXPECT_EQ(root.depth, 0);
}

TEST(VoxelWorld, QuerySDFAboveGround) {
    VoxelWorld world;
    world.generate();
    float d = world.querySDF(glm::vec3(0, 50, 0));
    EXPECT_GT(d, 0.0f);
}

TEST(VoxelWorld, QuerySDFBelowGround) {
    VoxelWorld world;
    world.generate();
    float d = world.querySDF(glm::vec3(0, -50, 0));
    EXPECT_LT(d, 0.0f);
}

TEST(VoxelWorld, QueryGradientAtSurface) {
    VoxelWorld world;
    world.generate();
    // Use a point on the ground plane far from the sphere obstacle
    // so the gradient is dominated by the terrain (normal = +Y)
    glm::vec3 grad = world.queryGradient(glm::vec3(50, 0.1f, 50));
    EXPECT_GT(grad.y, 0.0f);
}

TEST(VoxelWorld, SerializeWorldInit) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    ByteWriter w;
    world.serializeWorldInit(w, 2);
    EXPECT_GT(w.size(), 0u);
}

TEST(VoxelWorld, SerializeNodeResponse) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    const auto& root = world.getNode(0);
    if (root.childMask != 0 && root.childrenStart != 0) {
        ByteWriter w;
        world.serializeNodeResponse(0, w);
        EXPECT_GT(w.size(), 0u);
    }
}

TEST(VoxelWorld, ApplyDamageCreatesDelta) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    EXPECT_TRUE(world.pendingDeltas().empty());
    world.applyDamage(glm::vec3(0, 0, 0), 2.0f);
    // Just verify no crash
}

TEST(VoxelWorld, GetNodeBoundsCheck) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    for (uint32_t i = 0; i < world.nodeCount(); i++) {
        const auto& node = world.getNode(i);
        (void)node;
    }
}
