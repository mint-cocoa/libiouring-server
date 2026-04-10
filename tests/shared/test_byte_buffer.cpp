#include <gtest/gtest.h>
#include "shared/byte_buffer.h"

using namespace voxelarena;

TEST(ByteWriter, WriteU8) {
    ByteWriter w;
    w.writeU8(0x42);
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(w.data()[0], 0x42);
}

TEST(ByteWriter, WriteU16LittleEndian) {
    ByteWriter w;
    w.writeU16(0x1234);
    ASSERT_EQ(w.size(), 2u);
    EXPECT_EQ(w.data()[0], 0x34);
    EXPECT_EQ(w.data()[1], 0x12);
}

TEST(ByteWriter, WriteU32LittleEndian) {
    ByteWriter w;
    w.writeU32(0xDEADBEEF);
    ASSERT_EQ(w.size(), 4u);
    EXPECT_EQ(w.data()[0], 0xEF);
    EXPECT_EQ(w.data()[1], 0xBE);
    EXPECT_EQ(w.data()[2], 0xAD);
    EXPECT_EQ(w.data()[3], 0xDE);
}

TEST(ByteBuffer, RoundtripU8) {
    ByteWriter w;
    w.writeU8(255);
    ByteReader r(w.data(), w.size());
    EXPECT_EQ(r.readU8(), 255);
    EXPECT_FALSE(r.hasRemaining(1));
}

TEST(ByteBuffer, RoundtripU16) {
    ByteWriter w;
    w.writeU16(0xABCD);
    ByteReader r(w.data(), w.size());
    EXPECT_EQ(r.readU16(), 0xABCD);
}

TEST(ByteBuffer, RoundtripU32) {
    ByteWriter w;
    w.writeU32(123456789);
    ByteReader r(w.data(), w.size());
    EXPECT_EQ(r.readU32(), 123456789u);
}

TEST(ByteBuffer, RoundtripF32) {
    ByteWriter w;
    w.writeF32(3.14f);
    ByteReader r(w.data(), w.size());
    EXPECT_FLOAT_EQ(r.readF32(), 3.14f);
}

TEST(ByteBuffer, RoundtripVec3) {
    ByteWriter w;
    glm::vec3 v(1.0f, -2.5f, 3.14f);
    w.writeVec3(v);
    ASSERT_EQ(w.size(), 12u);
    ByteReader r(w.data(), w.size());
    glm::vec3 out = r.readVec3();
    EXPECT_FLOAT_EQ(out.x, 1.0f);
    EXPECT_FLOAT_EQ(out.y, -2.5f);
    EXPECT_FLOAT_EQ(out.z, 3.14f);
}

TEST(ByteBuffer, RoundtripMultipleValues) {
    ByteWriter w;
    w.writeU8(1);
    w.writeU16(1000);
    w.writeU32(999999);
    w.writeF32(-1.5f);
    w.writeVec3(glm::vec3(10, 20, 30));

    ByteReader r(w.data(), w.size());
    EXPECT_EQ(r.readU8(), 1);
    EXPECT_EQ(r.readU16(), 1000);
    EXPECT_EQ(r.readU32(), 999999u);
    EXPECT_FLOAT_EQ(r.readF32(), -1.5f);
    glm::vec3 v = r.readVec3();
    EXPECT_FLOAT_EQ(v.x, 10.0f);
    EXPECT_FLOAT_EQ(v.y, 20.0f);
    EXPECT_FLOAT_EQ(v.z, 30.0f);
    EXPECT_FALSE(r.hasRemaining(1));
}

TEST(ByteBuffer, WriteBytes) {
    ByteWriter w;
    uint8_t blob[] = {0xAA, 0xBB, 0xCC};
    w.writeBytes(blob, 3);
    ASSERT_EQ(w.size(), 3u);

    ByteReader r(w.data(), w.size());
    uint8_t out[3];
    r.readBytes(out, 3);
    EXPECT_EQ(out[0], 0xAA);
    EXPECT_EQ(out[1], 0xBB);
    EXPECT_EQ(out[2], 0xCC);
}

TEST(ByteReader, HasRemaining) {
    ByteWriter w;
    w.writeU32(0);
    ByteReader r(w.data(), w.size());
    EXPECT_TRUE(r.hasRemaining(4));
    EXPECT_FALSE(r.hasRemaining(5));
    r.readU16();
    EXPECT_TRUE(r.hasRemaining(2));
    EXPECT_FALSE(r.hasRemaining(3));
}
