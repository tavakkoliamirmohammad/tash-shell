// Real SshClient — OpenSSH wrapper with ControlMaster multiplexing.
// See include/tash/cluster/ssh_client.h for the contract.
//
// Argv builders are pure (tested directly). The fork+exec capture
// helper lives here because safe_exec in src/util/safe_exec.cpp only
// captures stdout; ssh stderr carries Duo prompts and error messages
// that we need to surface.

#include "tash/cluster/ssh_client.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>
#include <system_error>
#include <vector>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Argv builders
// ══════════════════════════════════════════════════════════════════════════════

namespace {

std::vector<std::string> base_ssh_flags(const SshFlags& f) {
    std::vector<std::string> a;
    a.push_back("-o"); a.push_back("ControlMaster=auto");
    a.push_back("-o"); a.push_back("ControlPath=" + (f.socket_dir / "tash-%C").string());
    a.push_back("-o"); a.push_back("ControlPersist=yes");
    if (f.batch_mode) {
        a.push_back("-o"); a.push_back("BatchMode=yes");
    }
    return a;
}

long long now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

}  // namespace

std::vector<std::string> build_run_argv(const SshFlags& f,
                                           const std::vector<std::string>& remote) {
    std::vector<std::string> a = {"ssh"};
    auto base = base_ssh_flags(f);
    a.insert(a.end(), base.begin(), base.end());
    a.push_back(f.ssh_host);
    a.insert(a.end(), remote.begin(), remote.end());
    return a;
}

std::vector<std::string> build_master_check_argv(const SshFlags& f) {
    std::vector<std::string> a = {"ssh"};
    auto base = base_ssh_flags(f);
    a.insert(a.end(), base.begin(), base.end());
    a.push_back("-O");
    a.push_back("check");
    a.push_back(f.ssh_host);
    return a;
}

std::vector<std::string> build_connect_argv(const SshFlags& f) {
    // Master mode, no remote command, backgrounded after auth.
    // BatchMode deliberately off — the user might need to type a
    // password + Duo response. ControlMaster=auto so the master
    // socket is created.
    SshFlags local = f;
    local.batch_mode = false;
    std::vector<std::string> a = {"ssh"};
    auto base = base_ssh_flags(local);
    a.insert(a.end(), base.begin(), base.end());
    a.push_back("-M"); a.push_back("-N"); a.push_back("-f");
    a.push_back(f.ssh_host);
    return a;
}

std::vector<std::string> build_disconnect_argv(const SshFlags& f) {
    std::vector<std::string> a = {"ssh"};
    auto base = base_ssh_flags(f);
    a.insert(a.end(), base.begin(), base.end());
    a.push_back("-O");
    a.push_back("exit");
    a.push_back(f.ssh_host);
    return a;
}

// ══════════════════════════════════════════════════════════════════════════════
// install_remote_file — base64-encoded one-shot transport
// ══════════════════════════════════════════════════════════════════════════════

namespace {

constexpr char kB64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const unsigned x = (static_cast<unsigned char>(in[i])   << 16) |
                           (static_cast<unsigned char>(in[i+1]) <<  8) |
                            static_cast<unsigned char>(in[i+2]);
        out += kB64Alphabet[(x >> 18) & 0x3f];
        out += kB64Alphabet[(x >> 12) & 0x3f];
        out += kB64Alphabet[(x >>  6) & 0x3f];
        out += kB64Alphabet[ x        & 0x3f];
    }
    if (i < in.size()) {
        unsigned x = static_cast<unsigned char>(in[i]) << 16;
        if (i + 1 < in.size()) x |= static_cast<unsigned char>(in[i+1]) << 8;
        out += kB64Alphabet[(x >> 18) & 0x3f];
        out += kB64Alphabet[(x >> 12) & 0x3f];
        out += (i + 1 < in.size()) ? kB64Alphabet[(x >> 6) & 0x3f] : '=';
        out += '=';
    }
    return out;
}

// Shell-quote a path for safe embedding in a single-quoted sh string.
// POSIX sh single-quoted strings end at the first ' — so we escape any
// ' in the path as '\''. Nothing else needs escaping inside '…'.
std::string sh_single_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += '\'';
    return out;
}

}  // namespace

std::vector<std::string> build_install_file_argv(const std::string& content,
                                                   const std::string& remote_path) {
    // `sh -c 'mkdir -p <dir> && printf %s <b64> | base64 -d > <path> && chmod 0755 <path>'`
    const auto slash = remote_path.find_last_of('/');
    const std::string parent =
        (slash == std::string::npos) ? "." : remote_path.substr(0, slash);

    const std::string b64 = base64_encode(content);

    std::string cmd;
    cmd += "mkdir -p -- ";       cmd += sh_single_quote(parent);
    cmd += " && printf %s ";     cmd += sh_single_quote(b64);
    cmd += " | base64 -d > ";    cmd += sh_single_quote(remote_path);
    cmd += " && chmod 0755 ";    cmd += sh_single_quote(remote_path);

    return {"/bin/sh", "-c", cmd};
}

bool install_remote_file(ISshClient& ssh,
                          const std::string& cluster,
                          const std::string& content,
                          const std::string& remote_path) {
    const auto r = ssh.run(cluster,
                             build_install_file_argv(content, remote_path),
                             std::chrono::seconds{15});
    return r.exit_code == 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// Process-level spawner (local — safe_exec doesn't give us stderr).
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// Spawn argv, capture stdout + stderr, enforce wall-clock timeout.
SshResult spawn_capture(const std::vector<std::string>& argv,
                          std::chrono::milliseconds timeout_ms) {
    SshResult r{};
    r.exit_code = -1;
    if (argv.empty()) return r;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe(out_pipe) < 0) return r;
    if (::pipe(err_pipe) < 0) { ::close(out_pipe[0]); ::close(out_pipe[1]); return r; }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        ::close(err_pipe[0]); ::close(err_pipe[1]);
        return r;
    }

    if (pid == 0) {
        // Child — splice pipes onto stdout/stderr and exec.
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        ::close(err_pipe[0]); ::close(err_pipe[1]);
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        ::execvp(c_argv[0], c_argv.data());
        _exit(127);  // exec failed
    }

    // Parent
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    const long long deadline_ms =
        timeout_ms.count() > 0 ? now_ms() + timeout_ms.count() : -1;

    struct pollfd pfd[2] = {
        {out_pipe[0], POLLIN, 0},
        {err_pipe[0], POLLIN, 0},
    };
    bool out_open = true, err_open = true;
    char buf[4096];

    while (out_open || err_open) {
        int to = -1;
        if (deadline_ms > 0) {
            const long long remaining = deadline_ms - now_ms();
            if (remaining <= 0) {
                // Timeout. Kill, then fall through to waitpid so we reap.
                ::kill(pid, SIGKILL);
                break;
            }
            to = static_cast<int>(remaining);
        }
        const int n = ::poll(pfd, 2, to);
        if (n < 0) {
            if (errno == EINTR) continue;
            // Unexpected poll error (EBADF, ENOMEM, etc.). The child
            // is still running and the subsequent waitpid() below
            // blocks with flag 0 — without this kill we'd hang
            // forever. Treat poll errors like a timeout.
            ::kill(pid, SIGKILL);
            break;
        }
        if (n == 0) {
            ::kill(pid, SIGKILL);
            break;
        }
        auto drain = [&](int idx, bool& open, std::string& sink) {
            if (!(pfd[idx].revents & (POLLIN | POLLHUP))) return;
            ssize_t k = ::read(pfd[idx].fd, buf, sizeof(buf));
            if (k > 0) sink.append(buf, static_cast<std::size_t>(k));
            else      { open = false; pfd[idx].fd = -1; }
        };
        drain(0, out_open, r.out);
        drain(1, err_open, r.err);
    }
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFEXITED(status))        r.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exit_code = 128 + WTERMSIG(status);
    return r;
}

// Spawn argv with the child inheriting the parent's stdio (terminal).
// Used by connect() so ssh's password + Duo prompts reach the user
// directly instead of being captured into a pipe the caller never
// displays. Waits up to `timeout_ms`; kills the child on timeout.
int spawn_inherit(const std::vector<std::string>& argv,
                   std::chrono::milliseconds timeout_ms) {
    if (argv.empty()) return -1;
    pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        ::execvp(c_argv[0], c_argv.data());
        _exit(127);
    }
    const long long deadline =
        timeout_ms.count() > 0 ? now_ms() + timeout_ms.count() : -1;
    while (true) {
        int status = 0;
        pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status))        return WEXITSTATUS(status);
            if (WIFSIGNALED(status))      return 128 + WTERMSIG(status);
            return -1;
        }
        if (r < 0) return -1;
        if (deadline > 0 && now_ms() >= deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            return -1;
        }
        struct timespec ts{0, 100'000'000};  // 100ms
        ::nanosleep(&ts, nullptr);
    }
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// SshClientReal
// ══════════════════════════════════════════════════════════════════════════════

namespace {

class SshClientReal : public ISshClient {
public:
    SshClientReal(HostResolver resolver, std::filesystem::path socket_dir)
        : resolve_(std::move(resolver)), sockets_(std::move(socket_dir)) {
        std::error_code ec;
        std::filesystem::create_directories(sockets_, ec);
    }

    SshResult run(const std::string& cluster,
                   const std::vector<std::string>& argv,
                   std::chrono::milliseconds timeout) override {
        SshFlags f{sockets_, resolve_(cluster), /*batch*/true};
        return spawn_capture(build_run_argv(f, argv), timeout);
    }

    bool master_alive(const std::string& cluster) override {
        SshFlags f{sockets_, resolve_(cluster), /*batch*/true};
        const auto r = spawn_capture(build_master_check_argv(f),
                                        std::chrono::seconds{2});
        return r.exit_code == 0;
    }

    // Pre-warm the ControlMaster interactively. MUST inherit stdio so
    // the user can see password + Duo prompts on their terminal; a
    // captured pipe eats the prompts and ssh stalls until we kill it.
    void connect(const std::string& cluster) override {
        SshFlags f{sockets_, resolve_(cluster), /*batch*/false};
        (void)spawn_inherit(build_connect_argv(f), std::chrono::minutes{2});
    }

    void disconnect(const std::string& cluster) override {
        SshFlags f{sockets_, resolve_(cluster), /*batch*/true};
        (void)spawn_capture(build_disconnect_argv(f), std::chrono::seconds{5});
    }

private:
    HostResolver            resolve_;
    std::filesystem::path   sockets_;
};

}  // namespace

std::unique_ptr<ISshClient> make_ssh_client(HostResolver host_resolver,
                                              std::filesystem::path socket_dir) {
    return std::make_unique<SshClientReal>(std::move(host_resolver),
                                             std::move(socket_dir));
}

}  // namespace tash::cluster
