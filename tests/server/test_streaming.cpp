#include <gtest/gtest.h>
#include "server/streaming_service.h"
#include "server/voxel_world.h"
#include <shared/byte_buffer.h>

using namespace voxelarena;

TEST(StreamingService, OnNodeRequestQueues) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    StreamingService svc(world);

    ByteWriter reqWriter;
    reqWriter.writeU32(1);   // requestCount = 1
    reqWriter.writeU32(0);   // nodeId = 0 (root)

    auto data = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(reqWriter.data()),
        reqWriter.size()
    );
    svc.onNodeRequest(1001, data);

    EXPECT_TRUE(svc.hasPendingRequests(1001));
}

TEST(StreamingService, ProcessQueuesProducesResponse) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    StreamingService svc(world);

    const auto& root = world.getNode(0);
    if (root.childMask == 0) {
        GTEST_SKIP() << "Root has no children";
    }

    ByteWriter reqWriter;
    reqWriter.writeU32(1);
    reqWriter.writeU32(0);

    auto data = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(reqWriter.data()),
        reqWriter.size()
    );
    svc.onNodeRequest(1001, data);

    auto responses = svc.processQueuesCollect(1001);
    EXPECT_FALSE(responses.empty());
}

TEST(StreamingService, ThrottlingLimitsPerFrame) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    StreamingService svc(world);

    ByteWriter reqWriter;
    reqWriter.writeU32(10);
    for (int i = 0; i < 10; i++) {
        reqWriter.writeU32(0);
    }

    auto data = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(reqWriter.data()),
        reqWriter.size()
    );
    svc.onNodeRequest(1001, data);

    auto responses = svc.processQueuesCollect(1001);
    EXPECT_LE(responses.size(), 4u);
}

TEST(StreamingService, WorldInitProducesData) {
    VoxelWorld world;
    world.generate(42, 128.0f, 4);
    StreamingService svc(world);

    ByteWriter out;
    svc.collectWorldInit(out);
    EXPECT_GT(out.size(), 0u);
}
