// TASH_CLUSTER_DEMO=1 runtime wiring — minimal in-memory seam impls
// that make every command succeed with sensible defaults.

#include "tash/cluster/demo_mode.h"

#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/notifier.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/slurm_ops.h"
#include "tash/cluster/ssh_client.h"
#include "tash/cluster/tmux_ops.h"
#include "tash/cluster/types.h"
#include "tash/plugins/cluster_watcher_hook_provider.h"
#include "tash/shell.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Seam impls (file-local; nothing from here escapes)
// ══════════════════════════════════════════════════════════════════════════════

namespace {

class DemoSshClient : public ISshClient {
public:
    SshResult run(const std::string&, const std::vector<std::string>&,
                   std::chrono::milliseconds) override {
        return SshResult{0, "", ""};
    }
    bool master_alive(const std::string& c) override { return alive_[c]; }
    void connect   (const std::string& c) override { alive_[c] = true;  }
    void disconnect(const std::string& c) override { alive_[c] = false; }
private:
    std::unordered_map<std::string, bool> alive_;
};

class DemoSlurmOps : public ISlurmOps {
public:
    SubmitResult sbatch(const SubmitSpec& s, ISshClient&) override {
        const std::string id = std::to_string(next_id_++);
        JobState j;
        j.jobid     = id;
        j.state     = "R";
        j.node      = "demo-n" + id;
        j.time_left = s.time.empty() ? "01:00:00" : s.time;
        jobs_[id] = std::move(j);
        return SubmitResult{id, "Submitted batch job " + id + " (demo)"};
    }

    std::optional<std::vector<JobState>>
    squeue(const std::string&, ISshClient&) override {
        std::vector<JobState> out;
        out.reserve(jobs_.size());
        for (const auto& kv : jobs_) out.push_back(kv.second);
        return out;
    }

    std::optional<std::vector<PartitionState>>
    sinfo(const std::string&,
           const std::string& partition,
           ISshClient&) override {
        return std::vector<PartitionState>{PartitionState{
            partition, "up", 4,
            {"gpu:a100:1", "gpu:a100:1", "gpu:a100:1", "gpu:a100:1"}
        }};
    }

    bool scancel(const std::string&, const std::string& jobid, ISshClient&) override {
        jobs_.erase(jobid);
        return true;
    }

private:
    std::int64_t                       next_id_ = 10000;
    std::map<std::string, JobState>    jobs_;
};

class DemoTmuxOps : public ITmuxOps {
public:
    bool new_session(const RemoteTarget&, const std::string& session,
                      const std::string&, ISshClient&) override {
        sessions_.insert(session);
        return true;
    }

    bool new_window(const RemoteTarget&, const std::string& session,
                     const std::string& window, const std::string&,
                     const std::string&, ISshClient&) override {
        alive_.insert(session + "/" + window);
        return true;
    }

    std::vector<SessionInfo> list_sessions(const RemoteTarget&, ISshClient&) override {
        std::vector<SessionInfo> out;
        for (const auto& s : sessions_) out.push_back(SessionInfo{s, 0});
        return out;
    }

    void kill_window(const RemoteTarget&, const std::string& session,
                      const std::string& window, ISshClient&) override {
        alive_.erase(session + "/" + window);
    }

    bool is_window_alive(const RemoteTarget&, const std::string& session,
                           const std::string& window, ISshClient&) override {
        return alive_.count(session + "/" + window) > 0;
    }

    void exec_attach(const RemoteTarget&, const std::string&,
                      const std::string&) override {
        // Real impl would replace the process image; demo just logs.
        std::cerr << "[demo cluster] exec_attach (noop)\n";
    }

private:
    std::unordered_set<std::string> sessions_;
    std::unordered_set<std::string> alive_;
};

class DemoNotifier : public INotifier {
public:
    void desktop(const std::string& title, const std::string& body) override {
        std::cerr << "[demo notify] " << title << ": " << body << "\n";
    }
    void bell() override {}
};

class DemoPrompt : public IPrompt {
public:
    char choice(const std::string&, const std::string&) override { return 'k'; }
};

class DemoClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() override { return t_; }
    void sleep_for(std::chrono::milliseconds d) override { t_ += d; }
private:
    std::chrono::steady_clock::time_point t_ = std::chrono::steady_clock::now();
};

// ══════════════════════════════════════════════════════════════════════════════
// DemoMode — bundles Config + Registry + seams + ClusterEngine
// ══════════════════════════════════════════════════════════════════════════════

struct DemoMode {
    Config                      cfg;
    Registry                    reg;
    DemoSshClient               ssh;
    DemoSlurmOps                slurm;
    DemoTmuxOps                 tmux;
    DemoNotifier                notify;
    DemoPrompt                  prompt;
    DemoClock                   clock;
    ClusterEngine               engine;
    ClusterWatcherHookProvider  watcher_hook;

    DemoMode()
        : cfg(demo_config()),
          reg(),
          ssh(), slurm(), tmux(), notify(), prompt(), clock(),
          engine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock),
          watcher_hook(reg, default_watcher_factory()) {
        // Prove the hook-provider lifecycle end-to-end in demo mode.
        // Factory is NoOp today (placeholder until the real ssh tail -F
        // source lands in M3.3/M3.4); the call below just wires the
        // startup/shutdown machinery so demo state mirrors production.
        ShellState state{};
        watcher_hook.on_startup(state);
    }

    ~DemoMode() {
        ShellState state{};
        watcher_hook.on_exit(state);
    }
};

std::unique_ptr<DemoMode> g_demo;

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Public API
// ══════════════════════════════════════════════════════════════════════════════

Config demo_config() {
    Config c;
    c.defaults.workspace_base     = "/demo-scratch";
    c.defaults.default_preset     = "demo-claude";
    c.defaults.control_persist    = "yes";
    c.defaults.notify_silence_sec = 120;

    c.clusters.push_back({"demo-cluster", "demo-ssh-alias",
                           "Demo cluster (in-memory fakes; no real SSH)"});

    Resource a100;
    a100.name         = "a100";
    a100.kind         = ResourceKind::Gpu;
    a100.description  = "Demo A100 resource";
    a100.default_time = "1:00:00";
    a100.default_cpus = 4;
    a100.default_mem  = "16G";
    a100.routes.push_back({"demo-cluster", "demo-account", "demo-partition",
                            "demo-qos", "gpu:a100:1"});
    c.resources.push_back(std::move(a100));

    Preset p;
    p.name    = "demo-claude";
    p.command = "echo 'hello from demo'";
    c.presets.push_back(std::move(p));

    return c;
}

void install_demo_engine() {
    uninstall_demo_engine();               // idempotent
    g_demo = std::make_unique<DemoMode>();
    set_active_engine(&g_demo->engine);
}

void uninstall_demo_engine() {
    set_active_engine(nullptr);
    g_demo.reset();
}

bool demo_engine_installed() { return g_demo != nullptr; }

}  // namespace tash::cluster
