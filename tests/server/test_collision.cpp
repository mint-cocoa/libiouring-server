#include <gtest/gtest.h>
#include "server/collision.h"
#include "server/voxel_world.h"

using namespace voxelarena;

class CollisionTest : public ::testing::Test {
protected:
    void SetUp() override {
        world.generate(42, 128.0f, 4);
    }
    VoxelWorld world;
};

TEST_F(CollisionTest, MoveAndSlidePreventsFallThroughFloor) {
    Collision col(world);
    glm::vec3 pos(10, 1.0f, 10);  // away from sphere obstacle
    glm::vec3 vel(0, -20.0f, 0);
    glm::vec3 result = col.moveAndSlide(pos, vel, 1.0f / 60.0f);
    EXPECT_GE(result.y, -0.5f);
}

TEST_F(CollisionTest, MoveAndSlideAllowsFreeMovementInAir) {
    Collision col(world);
    glm::vec3 pos(50, 50.0f, 50);
    glm::vec3 vel(5.0f, 0, 0);
    glm::vec3 result = col.moveAndSlide(pos, vel, 1.0f / 60.0f);
    EXPECT_GT(result.x, pos.x);
    EXPECT_NEAR(result.y, pos.y, 0.1f);
}

TEST_F(CollisionTest, RaycastHitsFloor) {
    Collision col(world);
    auto hit = col.raycast(glm::vec3(50, 10, 50), glm::vec3(0, -1, 0), 50.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.point.y, 0.0f, 0.5f);
    EXPECT_GT(hit.normal.y, 0.5f);
}

TEST_F(CollisionTest, RaycastMissesInAir) {
    Collision col(world);
    auto hit = col.raycast(glm::vec3(50, 50, 50), glm::vec3(1, 0, 0), 10.0f);
    EXPECT_FALSE(hit.hit);
}
