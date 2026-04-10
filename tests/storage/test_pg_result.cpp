#include <serverstorage/PgResult.h>
#include <gtest/gtest.h>

using namespace serverstorage;

TEST(PgResultTest, DefaultConstructorIsInvalid) {
    PgResult result;
    EXPECT_FALSE(result.IsOk());
    EXPECT_FALSE(result.IsCommand());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.RowCount(), 0);
    EXPECT_EQ(result.ColumnCount(), 0);
    EXPECT_EQ(result.ErrorMessage(), "no result");
}

TEST(PgResultTest, NullptrIsInvalid) {
    PgResult result(nullptr);
    EXPECT_FALSE(result);
}

TEST(PgResultTest, MoveConstructor) {
    PgResult a;
    PgResult b(std::move(a));
    EXPECT_FALSE(b);
}

TEST(PgResultTest, MoveAssignment) {
    PgResult a;
    PgResult b;
    b = std::move(a);
    EXPECT_FALSE(b);
}

TEST(PgResultTest, GetStringOnNull) {
    PgResult result;
    EXPECT_TRUE(result.GetString(0, 0).empty());
    EXPECT_EQ(result.GetInt(0, 0), 0);
    EXPECT_EQ(result.GetInt64(0, 0), 0);
    EXPECT_TRUE(result.IsNull(0, 0));
}
