#include "tash/util/safe_exec.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tash::util {

namespace {

// Millisecond-resolution monotonic clock reading, used to compute the
// remaining budget across multiple poll() calls.
long long now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

} // namespace

ExecResult safe_exec(const std::vector<std::string>& argv, int timeout_ms, bool suppress_stderr) {
    ExecResult result;
    result.exit_code = -1;

    if (argv.empty()) return result;

    int pfd[2] = {-1, -1};
    if (::pipe(pfd) < 0) {
        return result;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pfd[0]);
        ::close(pfd[1]);
        return result;
    }

    if (pid == 0) {
        // Child: replace stdout with the pipe write end, leave stderr
        // attached to the parent's stderr. Close the read end so the
        // parent's EOF detection works when the child exits.
        ::close(pfd[0]);
        if (pfd[1] != STDOUT_FILENO) {
            ::dup2(pfd[1], STDOUT_FILENO);
            ::close(pfd[1]);
        }

        // Silence the child's stderr if requested. Used for probe calls
        // (`git rev-parse` in the prompt) where the error output is
        // expected when the command "fails" and has nothing useful to
        // tell the user.
        if (suppress_stderr) {
            int devnull = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (devnull >= 0) {
                ::dup2(devnull, STDERR_FILENO);
                if (devnull != STDERR_FILENO) ::close(devnull);
            }
        }

        // Build a C argv vector; the callee wants a null-terminated
        // array of char*. Strings are owned by the caller-provided
        // vector and remain valid up to the replace-image call.
        std::vector<char *> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto &s : argv) {
            c_argv.push_back(const_cast<char *>(s.c_str()));
        }
        c_argv.push_back(nullptr);

        ::execvp(c_argv[0], c_argv.data());
        // Only returns on failure.
        _exit(127);
    }

    // Parent.
    ::close(pfd[1]);

    // Make the read end non-blocking so a poll() loop with a timeout
    // works. Non-blocking is also safe for the no-timeout path, since
    // we'll block on poll() there too.
    int flags = ::fcntl(pfd[0], F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(pfd[0], F_SETFL, flags | O_NONBLOCK);
    }

    const long long deadline_ms = (timeout_ms > 0) ? now_ms() + timeout_ms : -1;
    bool timed_out = false;
    char buf[4096];

    for (;;) {
        int wait_ms = -1; // block indefinitely by default
        if (deadline_ms >= 0) {
            long long remaining = deadline_ms - now_ms();
            if (remaining <= 0) {
                timed_out = true;
                break;
            }
            wait_ms = static_cast<int>(remaining);
        }

        struct pollfd pfdarr = { pfd[0], POLLIN, 0 };
        int pr = ::poll(&pfdarr, 1, wait_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            // poll() timeout fired.
            timed_out = true;
            break;
        }
        if (pfdarr.revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = ::read(pfd[0], buf, sizeof(buf));
            if (n > 0) {
                result.stdout_text.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                // EOF - writer side closed. Done reading.
                break;
            }
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
    }

    ::close(pfd[0]);

    if (timed_out) {
        // Deliver SIGKILL -- the child may be ignoring SIGTERM. Reap
        // unconditionally so no zombie is left behind.
        ::kill(pid, SIGKILL);
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }

    if (timed_out) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }

    return result;
}

} // namespace tash::util
