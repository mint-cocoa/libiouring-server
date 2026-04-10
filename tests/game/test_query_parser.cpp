#include <servergame/QueryParser.h>

#include <gtest/gtest.h>

using namespace servergame::matchmaker;

TEST(QueryParserTest, ParseSimpleEqual) {
    auto conds = QueryParser::Parse("+region:asia");
    ASSERT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0].key, "region");
    EXPECT_EQ(conds[0].op, QueryParser::Op::kEq);
    EXPECT_EQ(std::get<std::string>(conds[0].value), "asia");
}

TEST(QueryParserTest, ParseNotEqual) {
    auto conds = QueryParser::Parse("-mode:ranked");
    ASSERT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0].op, QueryParser::Op::kNeq);
}

TEST(QueryParserTest, ParseGte) {
    auto conds = QueryParser::Parse("+skill:>=1000");
    ASSERT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0].op, QueryParser::Op::kGte);
    EXPECT_DOUBLE_EQ(std::get<double>(conds[0].value), 1000.0);
}

TEST(QueryParserTest, ParseLte) {
    auto conds = QueryParser::Parse("+skill:<=500");
    ASSERT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0].op, QueryParser::Op::kLte);
    EXPECT_DOUBLE_EQ(std::get<double>(conds[0].value), 500.0);
}

TEST(QueryParserTest, ParseGt) {
    auto conds = QueryParser::Parse("+level:>10");
    ASSERT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0].op, QueryParser::Op::kGt);
}

TEST(QueryParserTest, ParseLt) {
    auto conds = QueryParser::Parse("+level:<5");
    ASSERT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0].op, QueryParser::Op::kLt);
}

TEST(QueryParserTest, ParseMultipleConditions) {
    auto conds = QueryParser::Parse("+region:asia +skill:>=800 -mode:ranked");
    ASSERT_EQ(conds.size(), 3u);
    EXPECT_EQ(conds[0].key, "region");
    EXPECT_EQ(conds[1].key, "skill");
    EXPECT_EQ(conds[2].key, "mode");
}

TEST(QueryParserTest, ParseEmpty) {
    auto conds = QueryParser::Parse("");
    EXPECT_TRUE(conds.empty());
}

TEST(QueryParserTest, EvaluateStringMatch) {
    auto conds = QueryParser::Parse("+region:asia");
    QueryParser::Properties props{{"region"}, {"asia"}, {}, {}};
    EXPECT_TRUE(QueryParser::Evaluate(conds, props));

    QueryParser::Properties props2{{"region"}, {"eu"}, {}, {}};
    EXPECT_FALSE(QueryParser::Evaluate(conds, props2));
}

TEST(QueryParserTest, EvaluateNumericGte) {
    auto conds = QueryParser::Parse("+skill:>=1000");
    QueryParser::Properties match_props{{"skill"}, {}, {"skill"}, {1200.0}};
    EXPECT_TRUE(QueryParser::Evaluate(conds, match_props));

    QueryParser::Properties low_props{{"skill"}, {}, {"skill"}, {800.0}};
    EXPECT_FALSE(QueryParser::Evaluate(conds, low_props));
}

TEST(QueryParserTest, EvaluateNeq) {
    auto conds = QueryParser::Parse("-mode:ranked");
    QueryParser::Properties props{{"mode"}, {"casual"}, {}, {}};
    EXPECT_TRUE(QueryParser::Evaluate(conds, props));

    QueryParser::Properties props2{{"mode"}, {"ranked"}, {}, {}};
    EXPECT_FALSE(QueryParser::Evaluate(conds, props2));
}
