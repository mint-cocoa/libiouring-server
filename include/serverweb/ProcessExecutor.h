#pragma once

#include <expected>
#include <string>
#include <vector>

#include <sys/types.h>

namespace serverweb {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
};

class ProcessExecutor {
public:
    static std::expected<ProcessResult, std::string>
        Run(const std::vector<std::string>& args,
            const std::string& working_dir = "",
            int timeout_seconds = 120);

    static std::expected<pid_t, std::string>
        Start(const std::vector<std::string>& args,
              const std::string& working_dir = "");

    static void Stop(pid_t pid, int timeout_seconds = 10);

    static bool IsRunning(pid_t pid);
};

} // namespace serverweb
