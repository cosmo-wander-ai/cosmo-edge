#include "util/BoundedProcess.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <stdexcept>

extern char** environ;

namespace cosmo::util {
namespace {
    using Clock = std::chrono::steady_clock;
    struct Descriptors {
        std::array<int, 6> values{{-1, -1, -1, -1, -1, -1}};
        ~Descriptors() {
            for (auto& fd : values)
                Close(fd);
        }
        static void Close(int& fd) {
            if (fd >= 0) {
                close(fd);
                fd = -1;
            }
        }
    };

    struct SpawnState {
        posix_spawn_file_actions_t actions{};
        posix_spawnattr_t attributes{};
        SpawnState() {
            if (posix_spawn_file_actions_init(&actions) != 0)
                throw std::runtime_error("spawn_setup_failed");
            if (posix_spawnattr_init(&attributes) != 0) {
                posix_spawn_file_actions_destroy(&actions);
                throw std::runtime_error("spawn_setup_failed");
            }
        }
        ~SpawnState() {
            posix_spawn_file_actions_destroy(&actions);
            posix_spawnattr_destroy(&attributes);
        }
    };

    void Require(bool value) {
        if (!value)
            throw std::runtime_error("process_io_failed");
    }

    bool ReadPipe(int& fd, std::string& output, size_t limit) {
        char buffer[4096];
        for (;;) {
            const auto count = read(fd, buffer, sizeof(buffer));
            if (count > 0) {
                const auto size      = static_cast<size_t>(count);
                const auto remaining = limit - output.size();
                output.append(buffer, std::min(size, remaining));
                if (size > remaining)
                    return false;
            } else if (count == 0) {
                Descriptors::Close(fd);
                return true;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            } else if (errno != EINTR) {
                throw std::runtime_error("process_io_failed");
            }
        }
    }
}  // namespace

BoundedProcessResult RunBoundedProcess(const std::vector<std::string>& argv, const std::string& input,
                                       Clock::time_point deadline, const std::function<bool()>& cancelled,
                                       size_t outputLimit, size_t errorLimit) {
    BoundedProcessResult result;
    if (argv.empty() || argv.size() > 128 || input.size() > 64 * 1024 || outputLimit == 0 ||
        errorLimit == 0 || outputLimit > 1024 * 1024 || errorLimit > 1024 * 1024 ||
        std::any_of(argv.begin(), argv.end(), [](const auto& arg) {
            return arg.size() > 16384 || arg.find('\0') != std::string::npos;
        })) {
        result.failure = "invalid_process_request";
        return result;
    }
    Descriptors descriptors;
    pid_t pid   = -1;
    bool reaped = false;
    int status  = 0;
    try {
        if (Clock::now() >= deadline) {
            result.failure = "process_timeout";
            return result;
        }
        if (cancelled && cancelled()) {
            result.failure = "process_cancelled";
            return result;
        }
        Require(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors.values.data()) == 0);
        Require(pipe(descriptors.values.data() + 2) == 0);
        Require(pipe(descriptors.values.data() + 4) == 0);
        // Keep source descriptors above stdio, including in callers with closed stdin.
        for (auto& fd : descriptors.values) {
            if (fd <= STDERR_FILENO) {
                const int duplicate = fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
                Require(duplicate >= 0);
                close(fd);
                fd = duplicate;
            }
            Require(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
        }
        SpawnState spawn;
        for (const auto& mapping : {std::pair<int, int>{descriptors.values[1], STDIN_FILENO},
                                    {descriptors.values[3], STDOUT_FILENO},
                                    {descriptors.values[5], STDERR_FILENO}})
            Require(posix_spawn_file_actions_adddup2(&spawn.actions, mapping.first, mapping.second) == 0);
        for (int fd : descriptors.values)
            Require(posix_spawn_file_actions_addclose(&spawn.actions, fd) == 0);
        sigset_t defaults, mask;
        sigemptyset(&defaults);
        sigaddset(&defaults, SIGPIPE);
        sigemptyset(&mask);
        Require(posix_spawnattr_setsigdefault(&spawn.attributes, &defaults) == 0);
        Require(posix_spawnattr_setsigmask(&spawn.attributes, &mask) == 0);
        Require(posix_spawnattr_setpgroup(&spawn.attributes, 0) == 0);
        Require(posix_spawnattr_setflags(&spawn.attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
                                                                POSIX_SPAWN_SETSIGMASK) == 0);
        std::vector<char*> args;
        for (const auto& value : argv)
            args.push_back(const_cast<char*>(value.c_str()));
        args.push_back(nullptr);
        const int spawned =
            posix_spawnp(&pid, args[0], &spawn.actions, &spawn.attributes, args.data(), environ);
        if (spawned != 0) {
            pid            = -1;
            result.failure = "process_spawn_failed";
            return result;
        }
        for (const int index : {1, 3, 5})
            Descriptors::Close(descriptors.values[index]);
        for (const int index : {0, 2, 4})
            Require(fcntl(descriptors.values[index], F_SETFL, O_NONBLOCK) == 0);
#ifdef SO_NOSIGPIPE
        int noSigPipe = 1;
        setsockopt(descriptors.values[0], SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
        size_t written = 0;
        for (;;) {
            if (Clock::now() >= deadline) {
                result.failure = "process_timeout";
                break;
            }
            if (cancelled && cancelled()) {
                result.failure = "process_cancelled";
                break;
            }
            if (written == input.size())
                Descriptors::Close(descriptors.values[0]);
            std::array<pollfd, 3> fds{{{descriptors.values[0], POLLOUT, 0},
                                       {descriptors.values[2], POLLIN, 0},
                                       {descriptors.values[4], POLLIN, 0}}};
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            const auto rc =
                poll(fds.data(), fds.size(), static_cast<int>(std::clamp<int64_t>(remaining.count(), 0, 50)));
            if (rc < 0 && errno == EINTR)
                continue;
            Require(rc >= 0);
            if (fds[0].fd >= 0 && fds[0].revents) {
#ifdef MSG_NOSIGNAL
                constexpr int flags = MSG_NOSIGNAL;
#else
                constexpr int flags = 0;
#endif
                const auto count = send(fds[0].fd, input.data() + written, input.size() - written, flags);
                if (count > 0)
                    written += static_cast<size_t>(count);
                else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    Descriptors::Close(descriptors.values[0]);
            }
            if ((fds[1].fd >= 0 && fds[1].revents &&
                 !ReadPipe(descriptors.values[2], result.output, outputLimit)) ||
                (fds[2].fd >= 0 && fds[2].revents &&
                 !ReadPipe(descriptors.values[4], result.error, errorLimit))) {
                result.failure = "process_output_limit";
                break;
            }
            if (!reaped) {
                const auto waited = waitpid(pid, &status, WNOHANG);
                Require(waited >= 0 || errno == EINTR);
                reaped = waited == pid;
            }
            if (reaped && descriptors.values[2] < 0 && descriptors.values[4] < 0)
                break;
        }
    } catch (...) {
        result.failure = "process_io_failed";
    }
    if (pid > 0) {
        if (!result.failure.empty()) {
            // The child has a dedicated process group; no other engine process is targeted.
            kill(-pid, SIGKILL);
            if (!reaped)
                kill(pid, SIGKILL);
        }
        if (!reaped) {
            while (waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR)
                    break;
            }
        }
        if (result.failure.empty())
            result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return result;
}
}  // namespace cosmo::util
