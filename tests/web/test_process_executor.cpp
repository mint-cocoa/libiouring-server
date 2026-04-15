#include <gtest/gtest.h>
#include <serverweb/ProcessExecutor.h>

#include <thread>
#include <chrono>

using namespace serverweb;

TEST(ProcessExecutorTest, RunEchoCommand) {
    auto result = ProcessExecutor::Run({"echo", "hello"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->exit_code, 0);
    EXPECT_EQ(result->stdout_output, "hello\n");
    EXPECT_TRUE(result->stderr_output.empty());
}

TEST(ProcessExecutorTest, RunFailingCommand) {
    auto result = ProcessExecutor::Run({"false"});
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->exit_code, 0);
}

TEST(ProcessExecutorTest, RunNonexistentCommand) {
    auto result = ProcessExecutor::Run({"nonexistent_command_xyz"});
    EXPECT_FALSE(result.has_value());
}

TEST(ProcessExecutorTest, RunWithWorkingDirectory) {
    auto result = ProcessExecutor::Run({"pwd"}, "/tmp");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->exit_code, 0);
    EXPECT_EQ(result->stdout_output, "/tmp\n");
}

TEST(ProcessExecutorTest, CapturesStderr) {
    auto result = ProcessExecutor::Run({"sh", "-c", "echo err >&2"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->exit_code, 0);
    EXPECT_EQ(result->stderr_output, "err\n");
}

TEST(ProcessExecutorTest, StartAndStopProcess) {
    auto pid = ProcessExecutor::Start({"sleep", "60"});
    ASSERT_TRUE(pid.has_value());
    EXPECT_TRUE(ProcessExecutor::IsRunning(*pid));

    ProcessExecutor::Stop(*pid);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(ProcessExecutor::IsRunning(*pid));
}

TEST(ProcessExecutorTest, IsRunningReturnsFalseForInvalidPid) {
    EXPECT_FALSE(ProcessExecutor::IsRunning(999999));
}
