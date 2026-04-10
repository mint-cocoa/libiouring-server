#include <gtest/gtest.h>
#include "shared/sdf_ops.h"
#include <cmath>

using namespace voxelarena;

TEST(SdfSphere, AtCenter) {
    float d = sdfSphere(glm::vec3(0), glm::vec3(0), 5.0f);
    EXPECT_FLOAT_EQ(d, -5.0f);
}

TEST(SdfSphere, OnSurface) {
    float d = sdfSphere(glm::vec3(5, 0, 0), glm::vec3(0), 5.0f);
    EXPECT_NEAR(d, 0.0f, 1e-5f);
}

TEST(SdfSphere, Outside) {
    float d = sdfSphere(glm::vec3(10, 0, 0), glm::vec3(0), 5.0f);
    EXPECT_FLOAT_EQ(d, 5.0f);
}

TEST(SdfSphere, OffCenter) {
    float d = sdfSphere(glm::vec3(3, 0, 0), glm::vec3(1, 0, 0), 5.0f);
    EXPECT_FLOAT_EQ(d, -3.0f);
}

TEST(SdfBox, Inside) {
    float d = sdfBox(glm::vec3(0), glm::vec3(0), glm::vec3(1));
    EXPECT_LT(d, 0.0f);
}

TEST(SdfBox, OnFace) {
    float d = sdfBox(glm::vec3(1, 0, 0), glm::vec3(0), glm::vec3(1));
    EXPECT_NEAR(d, 0.0f, 1e-5f);
}

TEST(SdfBox, Outside) {
    float d = sdfBox(glm::vec3(2, 0, 0), glm::vec3(0), glm::vec3(1));
    EXPECT_NEAR(d, 1.0f, 1e-5f);
}

TEST(SdfBox, DiagonalOutside) {
    float d = sdfBox(glm::vec3(2, 2, 0), glm::vec3(0), glm::vec3(1));
    EXPECT_NEAR(d, std::sqrt(2.0f), 1e-5f);
}

TEST(SdfPlane, Above) {
    float d = sdfPlane(glm::vec3(0, 5, 0), glm::vec3(0, 1, 0), 0.0f);
    EXPECT_FLOAT_EQ(d, 5.0f);
}

TEST(SdfPlane, Below) {
    float d = sdfPlane(glm::vec3(0, -3, 0), glm::vec3(0, 1, 0), 0.0f);
    EXPECT_FLOAT_EQ(d, -3.0f);
}

TEST(SdfPlane, WithOffset) {
    float d = sdfPlane(glm::vec3(0, 5, 0), glm::vec3(0, 1, 0), 2.0f);
    EXPECT_FLOAT_EQ(d, 3.0f);
}

TEST(SdfCylinder, OnAxis) {
    float d = sdfCylinder(glm::vec3(0, 5, 0), glm::vec3(0, 0, 0), glm::vec3(0, 10, 0), 2.0f);
    EXPECT_NEAR(d, -2.0f, 1e-4f);
}

TEST(SdfCylinder, OnSurface) {
    float d = sdfCylinder(glm::vec3(2, 5, 0), glm::vec3(0, 0, 0), glm::vec3(0, 10, 0), 2.0f);
    EXPECT_NEAR(d, 0.0f, 1e-4f);
}

TEST(CSG, Union) {
    EXPECT_FLOAT_EQ(sdfUnion(-1.0f, 2.0f), -1.0f);
    EXPECT_FLOAT_EQ(sdfUnion(3.0f, -2.0f), -2.0f);
}

TEST(CSG, Subtract) {
    EXPECT_FLOAT_EQ(sdfSubtract(1.0f, -2.0f), 2.0f);
    EXPECT_FLOAT_EQ(sdfSubtract(-3.0f, 1.0f), -1.0f);
}

TEST(CSG, Intersect) {
    EXPECT_FLOAT_EQ(sdfIntersect(-1.0f, 2.0f), 2.0f);
    EXPECT_FLOAT_EQ(sdfIntersect(3.0f, -2.0f), 3.0f);
}

TEST(CSG, SmoothUnion) {
    float a = 1.0f, b = 2.0f;
    EXPECT_NEAR(sdfSmoothUnion(a, b, 0.0001f), std::min(a, b), 0.01f);
}

TEST(Gradient, SphereNormal) {
    auto grad = estimateGradient([](glm::vec3 p) {
        return sdfSphere(p, glm::vec3(0), 5.0f);
    }, glm::vec3(5, 0, 0));

    glm::vec3 n = glm::normalize(grad);
    EXPECT_NEAR(n.x, 1.0f, 0.01f);
    EXPECT_NEAR(n.y, 0.0f, 0.01f);
    EXPECT_NEAR(n.z, 0.0f, 0.01f);
}

TEST(Gradient, PlaneNormal) {
    auto grad = estimateGradient([](glm::vec3 p) {
        return sdfPlane(p, glm::vec3(0, 1, 0), 0.0f);
    }, glm::vec3(0, 3, 0));

    glm::vec3 n = glm::normalize(grad);
    EXPECT_NEAR(n.x, 0.0f, 0.01f);
    EXPECT_NEAR(n.y, 1.0f, 0.01f);
    EXPECT_NEAR(n.z, 0.0f, 0.01f);
}

TEST(ArenaMap, AboveGround) {
    float d = sdfArenaMap(glm::vec3(0, 50, 0));
    EXPECT_GT(d, 0.0f);
}

TEST(ArenaMap, BelowGround) {
    float d = sdfArenaMap(glm::vec3(0, -50, 0));
    EXPECT_LT(d, 0.0f);
}
