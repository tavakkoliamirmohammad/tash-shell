// PipedLineSource impl. See include/tash/cluster/piped_line_source.h.
//
// Design notes
// ────────────
// - We close the write-end of the pipe in the parent and stdout fd in
//   the child is wired to the write-end. When the parent closes the
//   read-end, the child's next write fails with EPIPE → it usually
//   exits on its own. If it doesn't, stop() sends SIGTERM; destructor
//   promotes to SIGKILL after a short grace.
// - next_line() reads up to 4 KiB at a time into a persistent buffer
//   and slices off the first complete line per call. Very long lines
//   without a newline eventually still get returned on EOF.
// - waitpid in the destructor uses a sleep-then-SIGKILL fallback; we
//   never block the shell for more than ~100 ms.

#include "tash/cluster/piped_line_source.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace tash::cluster {

namespace {

// Set O_CLOEXEC on an fd (defensive: pipes are CLOEXEC via pipe2 but
// double-check when supporting platforms lacking pipe2).
void set_cloexec(int fd) {
    if (fd < 0) return;
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags == -1) return;
    (void)::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

// Read more bytes into `buf` from fd; returns bytes read, 0 on EOF,
// -1 on error.
ssize_t read_more(int fd, std::string& buf) {
    char tmp[4096];
    ssize_t n;
    do {
        n = ::read(fd, tmp, sizeof(tmp));
    } while (n < 0 && errno == EINTR);
    if (n > 0) buf.append(tmp, static_cast<std::size_t>(n));
    return n;
}

}  // namespace

PipedLineSource::PipedLineSource(std::vector<std::string> argv) {
    if (argv.empty()) return;

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) < 0) return;
    set_cloexec(pipefd[0]);
    // Do NOT CLOEXEC pipefd[1] — it must survive exec in the child.

    const pid_t p = ::fork();
    if (p < 0) {
        ::close(pipefd[0]); ::close(pipefd[1]);
        return;
    }

    if (p == 0) {
        // Child. Start a new process group so we can later kill the
        // whole group (child + grandchildren) with a single signal.
        // Without this, a shell like /bin/sh -c 'sleep 60' would spawn
        // sleep as a grandchild; SIGKILL to the shell orphans sleep
        // and the parent test process hangs waiting for orphans to
        // exit (especially noticeable in containerized CI).
        ::setpgid(0, 0);

        // Detach stdin from the parent's terminal. Critical for the
        // watcher's `ssh tail -F` — without this the child ssh's
        // stdin stays connected to tash's terminal and fights the
        // REPL for keystrokes. The user sees their keyboard "lock"
        // (each keypress gets consumed by ssh and silently thrown
        // away inside the ssh protocol loop).
        const int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::close(devnull);
        }

        // Wire pipe write-end to stdout, close read-end.
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        // Leave stderr connected to parent's terminal so users see ssh
        // auth prompts / errors. For fully-batch sources that's fine
        // too (ssh's BatchMode suppresses prompts).

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        ::execvp(c_argv[0], c_argv.data());
        // execvp only returns on failure.
        _exit(127);
    }

    // Parent side: also call setpgid to close the race where the
    // child hasn't yet called setpgid when the parent tries to signal
    // the group. Either side succeeding is enough.
    ::setpgid(p, p);

    // Parent.
    ::close(pipefd[1]);          // only child writes
    read_fd_ = pipefd[0];
    pid_     = p;
}

PipedLineSource::~PipedLineSource() {
    stop();

    if (pid_ > 0) {
        // Grace period for the child to notice EPIPE / SIGTERM.
        for (int i = 0; i < 10; ++i) {
            int status = 0;
            const pid_t r = ::waitpid(pid_, &status, WNOHANG);
            if (r > 0 || r < 0) { pid_ = -1; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pid_ > 0) {
            ::kill(-pid_, SIGKILL);     // kill the entire process group
            int status = 0;
            (void)::waitpid(pid_, &status, 0);   // definitive reap of child
            pid_ = -1;
        }
    }
}

void PipedLineSource::stop() {
    const bool was = stopped_.exchange(true, std::memory_order_acq_rel);
    if (was) return;

    if (read_fd_ >= 0) {
        ::close(read_fd_);
        read_fd_ = -1;
    }
    if (pid_ > 0) {
        // SIGTERM the whole process group so grandchildren (e.g., a
        // `sleep` spawned by the child shell) are also signalled.
        // Negative pid means "send to process group".
        ::kill(-pid_, SIGTERM);
    }
}

std::optional<std::string> PipedLineSource::next_line() {
    while (true) {
        // Emit any complete line already in the buffer.
        const auto nl = rbuf_.find('\n');
        if (nl != std::string::npos) {
            std::string line = rbuf_.substr(0, nl);
            rbuf_.erase(0, nl + 1);
            // Strip trailing \r (CRLF-safe).
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }

        if (read_fd_ < 0) {
            // Source closed. If we have trailing non-newline content,
            // return it once as a final line; then EOF on next call.
            if (!rbuf_.empty()) {
                std::string line = std::move(rbuf_);
                rbuf_.clear();
                return line;
            }
            return std::nullopt;
        }

        const ssize_t n = read_more(read_fd_, rbuf_);
        if (n > 0) continue;   // loop; maybe we now have a newline

        // n == 0 → EOF. n < 0 → I/O error (read fd may have been
        // closed by stop() on another thread). Either way, close our
        // fd so subsequent calls fast-path to the "source closed" arm
        // above, then loop once more to drain rbuf_.
        if (read_fd_ >= 0) {
            ::close(read_fd_);
            read_fd_ = -1;
        }
    }
}

LineSource PipedLineSource::as_line_source(std::shared_ptr<PipedLineSource> src) {
    // Capture the shared_ptr by value so the process outlives the
    // callable itself (and any thread holding it).
    return [sp = std::move(src)]() mutable -> std::optional<std::string> {
        return sp->next_line();
    };
}

}  // namespace tash::cluster
