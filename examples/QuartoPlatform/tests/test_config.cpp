#include <gtest/gtest.h>
#include "config.h"
#include <fstream>

using namespace quarto;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        gateway_path_ = "/tmp/test_gateway.yaml";
        std::ofstream f(gateway_path_);
        f << R"(
mode: gateway
server:
  port: 8080
  workers: 2
auth:
  jwt_secret: test-secret
  github_client_id: test-id
  github_client_secret: test-secret-val
  github_redirect_uri: http://localhost:8080/auth/github/callback
  frontend_url: http://localhost:5173
proxy:
  routes:
    - path_prefix: "/api/"
      upstream_host: "editor"
      upstream_port: 8080
    - path_prefix: "/preview/"
      upstream_host: "editor"
      upstream_port: 4000
      websocket: true
static_sites:
  - root: "/srv/frontend"
    spa: true
  - root: "/srv/published"
redis:
  url: redis://localhost:6379
k8s:
  namespace: quarto
  editor_image: quarto-server:latest
  editor_config: /etc/quarto/editor.yaml
cleanup:
  idle_timeout: 1800
  interval: 300
)";
        f.close();

        editor_path_ = "/tmp/test_editor.yaml";
        std::ofstream e(editor_path_);
        e << R"(
mode: editor
server:
  port: 8080
  workers: 1
editor:
  workspace: /workspace
  published: /published
  user_id: alice
  preview_port: 4000
quarto:
  render_profile: production
  timeout: 120
)";
        e.close();
    }

    std::string gateway_path_;
    std::string editor_path_;
};

TEST_F(ConfigTest, LoadsGatewayConfig) {
    auto config = Config::Load(gateway_path_);
    EXPECT_EQ(config.mode, "gateway");
    EXPECT_EQ(config.server.port, 8080);
    EXPECT_EQ(config.server.workers, 2);
    EXPECT_EQ(config.auth.jwt_secret, "test-secret");
    EXPECT_EQ(config.auth.github_client_id, "test-id");
    EXPECT_EQ(config.proxy.routes.size(), 2);
    EXPECT_EQ(config.proxy.routes[0].path_prefix, "/api/");
    EXPECT_EQ(config.proxy.routes[0].upstream_host, "editor");
    EXPECT_EQ(config.redis.url, "redis://localhost:6379");
}

TEST_F(ConfigTest, LoadsEditorConfig) {
    auto config = Config::Load(editor_path_);
    EXPECT_EQ(config.mode, "editor");
    EXPECT_EQ(config.editor.workspace, "/workspace");
    EXPECT_EQ(config.editor.user_id, "alice");
    EXPECT_EQ(config.editor.preview_port, 4000);
    EXPECT_EQ(config.quarto.render_profile, "production");
}

TEST_F(ConfigTest, ThrowsOnMissingFile) {
    EXPECT_THROW(Config::Load("/nonexistent.yaml"), std::runtime_error);
}

TEST_F(ConfigTest, ExpandsEnvironmentVariables) {
    setenv("TEST_JWT_SECRET", "env-secret", 1);
    std::ofstream f("/tmp/test_env.yaml");
    f << R"(
mode: gateway
server:
  port: 8080
  workers: 1
auth:
  jwt_secret: ${TEST_JWT_SECRET}
  github_client_id: id
  github_client_secret: secret
  github_redirect_uri: http://localhost
  frontend_url: http://localhost
redis:
  url: redis://localhost:6379
k8s:
  namespace: quarto
  editor_image: img
  editor_config: cfg
cleanup:
  idle_timeout: 1800
  interval: 300
)";
    f.close();
    auto config = Config::Load("/tmp/test_env.yaml");
    EXPECT_EQ(config.auth.jwt_secret, "env-secret");
    unsetenv("TEST_JWT_SECRET");
}
