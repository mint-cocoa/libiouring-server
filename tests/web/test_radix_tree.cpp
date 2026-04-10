#include <serverweb/RadixTree.h>
#include <serverweb/Router.h>

#include <gtest/gtest.h>

#include <string>

using namespace serverweb;

class RadixTreeTest : public ::testing::Test {
protected:
    RadixTree tree_;
    int handler_id_ = 0;

    HttpHandler MakeHandler(int id) {
        return [this, id](RequestContext&) { handler_id_ = id; };
    }
};

// --- Static routes ---

TEST_F(RadixTreeTest, RootPath) {
    tree_.Insert(HttpMethod::kGet, "/", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/");
    ASSERT_NE(result.handler, nullptr);
    EXPECT_TRUE(result.path_exists);
}

TEST_F(RadixTreeTest, StaticRoutes) {
    tree_.Insert(HttpMethod::kGet, "/users", MakeHandler(1));
    tree_.Insert(HttpMethod::kGet, "/users/list", MakeHandler(2));
    tree_.Insert(HttpMethod::kPost, "/users", MakeHandler(3));

    auto r1 = tree_.Match(HttpMethod::kGet, "/users");
    ASSERT_NE(r1.handler, nullptr);

    auto r2 = tree_.Match(HttpMethod::kGet, "/users/list");
    ASSERT_NE(r2.handler, nullptr);

    auto r3 = tree_.Match(HttpMethod::kPost, "/users");
    ASSERT_NE(r3.handler, nullptr);
}

TEST_F(RadixTreeTest, NotFound) {
    tree_.Insert(HttpMethod::kGet, "/users", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/posts");
    EXPECT_EQ(result.handler, nullptr);
    EXPECT_FALSE(result.path_exists);
}

// --- 405 Method Not Allowed ---

TEST_F(RadixTreeTest, MethodNotAllowed) {
    tree_.Insert(HttpMethod::kGet, "/users", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kPost, "/users");
    EXPECT_EQ(result.handler, nullptr);
    EXPECT_TRUE(result.path_exists);  // path exists, wrong method -> 405
}

TEST_F(RadixTreeTest, RootMethodNotAllowed) {
    tree_.Insert(HttpMethod::kGet, "/", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kPost, "/");
    EXPECT_EQ(result.handler, nullptr);
    EXPECT_TRUE(result.path_exists);
}

// --- Parameter routes ---

TEST_F(RadixTreeTest, SingleParam) {
    tree_.Insert(HttpMethod::kGet, "/users/:id", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/users/42");
    ASSERT_NE(result.handler, nullptr);
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(result.params[0].first, "id");
    EXPECT_EQ(result.params[0].second, "42");
}

TEST_F(RadixTreeTest, MultipleParams) {
    tree_.Insert(HttpMethod::kGet, "/users/:uid/posts/:pid", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/users/5/posts/99");
    ASSERT_NE(result.handler, nullptr);
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(result.params[0].first, "uid");
    EXPECT_EQ(result.params[0].second, "5");
    EXPECT_EQ(result.params[1].first, "pid");
    EXPECT_EQ(result.params[1].second, "99");
}

TEST_F(RadixTreeTest, DeepNestedParams) {
    tree_.Insert(HttpMethod::kGet, "/a/:b/c/:d/e", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/a/X/c/Y/e");
    ASSERT_NE(result.handler, nullptr);
    ASSERT_EQ(result.params.size(), 2);
    EXPECT_EQ(result.params[0].first, "b");
    EXPECT_EQ(result.params[0].second, "X");
    EXPECT_EQ(result.params[1].first, "d");
    EXPECT_EQ(result.params[1].second, "Y");
}

// --- Static vs Param priority ---

TEST_F(RadixTreeTest, StaticOverParam) {
    // /users/new should match before /users/:id
    tree_.Insert(HttpMethod::kGet, "/users/new", MakeHandler(1));
    tree_.Insert(HttpMethod::kGet, "/users/:id", MakeHandler(2));

    auto r1 = tree_.Match(HttpMethod::kGet, "/users/new");
    ASSERT_NE(r1.handler, nullptr);
    EXPECT_TRUE(r1.params.empty());  // matched static, no params

    auto r2 = tree_.Match(HttpMethod::kGet, "/users/42");
    ASSERT_NE(r2.handler, nullptr);
    ASSERT_EQ(r2.params.size(), 1);
    EXPECT_EQ(r2.params[0].second, "42");
}

TEST_F(RadixTreeTest, StaticOverParamReverseInsert) {
    // Insert order shouldn't matter
    tree_.Insert(HttpMethod::kGet, "/users/:id", MakeHandler(2));
    tree_.Insert(HttpMethod::kGet, "/users/new", MakeHandler(1));

    auto r1 = tree_.Match(HttpMethod::kGet, "/users/new");
    ASSERT_NE(r1.handler, nullptr);
    EXPECT_TRUE(r1.params.empty());

    auto r2 = tree_.Match(HttpMethod::kGet, "/users/42");
    ASSERT_NE(r2.handler, nullptr);
    ASSERT_EQ(r2.params.size(), 1);
}

// --- Wildcard routes ---

TEST_F(RadixTreeTest, Wildcard) {
    tree_.Insert(HttpMethod::kGet, "/static/*filepath", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/static/css/style.css");
    ASSERT_NE(result.handler, nullptr);
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(result.params[0].first, "filepath");
    EXPECT_EQ(result.params[0].second, "css/style.css");
}

TEST_F(RadixTreeTest, WildcardSingleSegment) {
    tree_.Insert(HttpMethod::kGet, "/files/*path", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kGet, "/files/readme.txt");
    ASSERT_NE(result.handler, nullptr);
    ASSERT_EQ(result.params.size(), 1);
    EXPECT_EQ(result.params[0].first, "path");
    EXPECT_EQ(result.params[0].second, "readme.txt");
}

// --- Wildcard priority (lowest) ---

TEST_F(RadixTreeTest, StaticOverWildcard) {
    tree_.Insert(HttpMethod::kGet, "/files/*path", MakeHandler(1));
    tree_.Insert(HttpMethod::kGet, "/files/special", MakeHandler(2));

    auto r1 = tree_.Match(HttpMethod::kGet, "/files/special");
    ASSERT_NE(r1.handler, nullptr);
    EXPECT_TRUE(r1.params.empty());  // static match

    auto r2 = tree_.Match(HttpMethod::kGet, "/files/other.txt");
    ASSERT_NE(r2.handler, nullptr);
    ASSERT_EQ(r2.params.size(), 1);
    EXPECT_EQ(r2.params[0].second, "other.txt");
}

TEST_F(RadixTreeTest, ParamOverWildcard) {
    tree_.Insert(HttpMethod::kGet, "/api/*rest", MakeHandler(1));
    tree_.Insert(HttpMethod::kGet, "/api/:version", MakeHandler(2));

    // :version should match a single segment
    auto r1 = tree_.Match(HttpMethod::kGet, "/api/v2");
    ASSERT_NE(r1.handler, nullptr);
    ASSERT_EQ(r1.params.size(), 1);
    EXPECT_EQ(r1.params[0].first, "version");

    // wildcard should match multiple segments
    auto r2 = tree_.Match(HttpMethod::kGet, "/api/v2/users");
    ASSERT_NE(r2.handler, nullptr);
    ASSERT_EQ(r2.params.size(), 1);
    EXPECT_EQ(r2.params[0].first, "rest");
}

// --- Trailing slash ---

TEST_F(RadixTreeTest, TrailingSlash) {
    tree_.Insert(HttpMethod::kGet, "/users", MakeHandler(1));
    // "/users/" with trailing slash should not match "/users"
    auto result = tree_.Match(HttpMethod::kGet, "/users/");
    EXPECT_EQ(result.handler, nullptr);
}

// --- Empty tree ---

TEST_F(RadixTreeTest, EmptyTree) {
    auto result = tree_.Match(HttpMethod::kGet, "/anything");
    EXPECT_EQ(result.handler, nullptr);
    EXPECT_FALSE(result.path_exists);
}

// --- Multiple methods on same path ---

TEST_F(RadixTreeTest, MultipleMethodsSamePath) {
    tree_.Insert(HttpMethod::kGet, "/resource", MakeHandler(1));
    tree_.Insert(HttpMethod::kPost, "/resource", MakeHandler(2));
    tree_.Insert(HttpMethod::kPut, "/resource", MakeHandler(3));

    EXPECT_NE(tree_.Match(HttpMethod::kGet, "/resource").handler, nullptr);
    EXPECT_NE(tree_.Match(HttpMethod::kPost, "/resource").handler, nullptr);
    EXPECT_NE(tree_.Match(HttpMethod::kPut, "/resource").handler, nullptr);

    auto r = tree_.Match(HttpMethod::kDelete, "/resource");
    EXPECT_EQ(r.handler, nullptr);
    EXPECT_TRUE(r.path_exists);  // 405
}

// --- Param 405 ---

TEST_F(RadixTreeTest, ParamMethodNotAllowed) {
    tree_.Insert(HttpMethod::kGet, "/users/:id", MakeHandler(1));
    auto result = tree_.Match(HttpMethod::kDelete, "/users/42");
    EXPECT_EQ(result.handler, nullptr);
    EXPECT_TRUE(result.path_exists);
}
