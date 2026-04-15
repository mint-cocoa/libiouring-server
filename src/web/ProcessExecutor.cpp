#include <serverweb/ProcessExecutor.h>

#include <cerrno>
#include <cstring>
#include <array>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace serverweb {

static std::string ReadFd(int fd) {
    std::string result;
    std::array<char, 4096> buf{};
    ssize_t n;
    while ((n = ::read(fd, buf.data(), buf.size())) > 0) {
        result.append(buf.data(), n);
    }
    return result;
}

std::expected<ProcessResult, std::string>
ProcessExecutor::Run(const std::vector<std::string>& args,
                     const std::string& working_dir,
                     int timeout_seconds) {
    if (args.empty()) {
        return std::unexpected("empty command");
    }

    int stdout_pipe[2], stderr_pipe[2];
    if (::pipe(stdout_pipe) < 0 || ::pipe(stderr_pipe) < 0) {
        return std::unexpected(std::string("pipe failed: ") + strerror(errno));
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(stdout_pipe[0]); ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]); ::close(stderr_pipe[1]);
        return std::unexpected(std::string("fork failed: ") + strerror(errno));
    }

    if (pid == 0) {
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[1]);

        if (!working_dir.empty()) {
            if (::chdir(working_dir.c_str()) < 0) {
                _exit(127);
            }
        }

        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    auto stdout_str = ReadFd(stdout_pipe[0]);
    auto stderr_str = ReadFd(stderr_pipe[0]);
    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127 && stdout_str.empty()) {
        return std::unexpected("command not found: " + args[0]);
    }

    ProcessResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_output = std::move(stdout_str);
    result.stderr_output = std::move(stderr_str);
    return result;
}

std::expected<pid_t, std::string>
ProcessExecutor::Start(const std::vector<std::string>& args,
                       const std::string& working_dir) {
    if (args.empty()) {
        return std::unexpected("empty command");
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork failed: ") + strerror(errno));
    }

    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }

        if (!working_dir.empty()) {
            if (::chdir(working_dir.c_str()) < 0) {
                _exit(127);
            }
        }

        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    return pid;
}

void ProcessExecutor::Stop(pid_t pid, int timeout_seconds) {
    if (!IsRunning(pid)) return;

    ::kill(pid, SIGTERM);

    for (int i = 0; i < timeout_seconds * 10; ++i) {
        int status = 0;
        pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result != 0) return;
        ::usleep(100'000);
    }

    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
}

bool ProcessExecutor::IsRunning(pid_t pid) {
    return ::kill(pid, 0) == 0;
}

} // namespace serverweb
