// Real-cluster runtime wiring. See include/tash/cluster/real_mode.h for
// the public contract. This module is the production counterpart of
// demo_mode.cpp: same composition (Config + Registry + seams + engine
// + watcher-hook lifecycle), just with real ssh/slurm/tmux backends.

#include "tash/cluster/real_mode.h"

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"
#include "tash/cluster/notifier.h"
#include "tash/cluster/notifier_factory.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/slurm_ops.h"
#include "tash/cluster/ssh_client.h"
#include "tash/cluster/tmux_ops.h"
#include "tash/cluster/types.h"
#include "tash/plugins/cluster_watcher_hook_provider.h"
#include "tash/shell.h"
#include "tash/util/io.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace tash::cluster {

namespace {

// Non-blocking prompt. Reading from std::cin inside a tash builtin
// fights with replxx's raw-mode terminal state and hangs silently,
// and the user has no reliable way to know we're blocked on stdin.
//
// Return '\0' immediately so the engine falls through to its
// non-interactive default ("detach-and-keep" in the `cluster up`
// queued-too-long case). The message is still printed so the user
// knows what decision was made on their behalf.
class StdinPrompt : public IPrompt {
public:
    char choice(const std::string& message,
                const std::string& /*choices*/) override {
        std::fprintf(stderr, "tash: cluster: %s — auto-detaching; "
                             "check `cluster list` to monitor\n",
                             message.c_str());
        return '\0';
    }
};

// Resolves $TASH_CLUSTER_HOME or $HOME/.tash/cluster for the config
// directory. Returns std::nullopt if neither is set (no HOME on a
// CI container, for example) — caller treats that as "no config".
std::optional<std::filesystem::path> resolve_cluster_home() {
    if (const char* h = std::getenv("TASH_CLUSTER_HOME"); h && *h) {
        return std::filesystem::path{h};
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path{home} / ".tash" / "cluster";
    }
    return std::nullopt;
}

// ── RealMode — bundles Config + Registry + seams + ClusterEngine
struct RealMode {
    Config                              cfg;
    Registry                            reg;
    std::unique_ptr<ISshClient>         ssh;
    std::unique_ptr<ISlurmOps>          slurm;
    std::unique_ptr<ITmuxOps>           tmux;
    std::unique_ptr<INotifier>          notify;
    StdinPrompt                         prompt;
    RealClock                           clock;
    std::unique_ptr<ClusterEngine>      engine;
    std::unique_ptr<ClusterWatcherHookProvider> watcher_hook;

    std::filesystem::path               registry_path;

    RealMode(Config c,
             Registry r,
             std::filesystem::path socket_dir,
             std::filesystem::path reg_path)
        : cfg(std::move(c)),
          reg(std::move(r)),
          registry_path(std::move(reg_path)) {
        // Snapshot cfg by value for the HostResolver closure so it
        // doesn't depend on RealMode's `cfg` field alive — which is
        // the case while RealMode lives, but the closure outlives
        // teardown window in practice.
        auto resolver = [this](const std::string& cluster) -> std::string {
            if (const Cluster* cl = find_cluster(cfg, cluster)) {
                return cl->ssh_host;
            }
            return cluster;  // fall back to the literal name
        };
        ssh    = make_ssh_client(resolver, std::move(socket_dir));
        slurm  = make_slurm_ops();
        tmux   = make_tmux_ops();
        notify = make_notifier();

        engine = std::make_unique<ClusterEngine>(
            cfg, reg, *ssh, *slurm, *tmux, *notify, prompt, clock);

        // Persist registry after every state-mutating command and,
        // importantly, just before `cluster attach` exec-replaces
        // this process (the destructor-based save doesn't run in
        // that case). Uses atomic tmp+rename via Registry::save.
        const auto rp     = registry_path;
        Registry* reg_ptr = &reg;
        engine->set_save_callback([reg_ptr, rp]() {
            std::error_code ec;
            std::filesystem::create_directories(rp.parent_path(), ec);
            try {
                reg_ptr->save(rp);
            } catch (const std::exception& e) {
                tash::io::debug(std::string("cluster: registry save failed: ") + e.what());
            }
        });

        // ssh-tail watcher wiring — opt-in for v1 until glob/no-file
        // handling is more robust. When TASH_CLUSTER_WATCH=1 is set,
        // spawn the real watcher; otherwise use the NoOp factory so
        // stale/ended allocations in the registry can't hang tash's
        // REPL on startup by spawning doomed `ssh tail -F` threads.
        const bool watch =
            []() { const char* v = std::getenv("TASH_CLUSTER_WATCH");
                     return v && std::string(v) == "1"; }();
        if (watch) {
            auto cluster_resolver = [this](const Allocation& a) -> std::string {
                if (const Cluster* cl = find_cluster(cfg, a.cluster))
                    return cl->ssh_host;
                return a.cluster;
            };
            auto event_dir_resolver = [](const Allocation&) -> std::string {
                return "$HOME/.tash-cluster/events";
            };
            watcher_hook = std::make_unique<ClusterWatcherHookProvider>(
                reg,
                make_ssh_tail_watcher_factory(std::move(cluster_resolver),
                                                std::move(event_dir_resolver),
                                                *notify));
        } else {
            watcher_hook = std::make_unique<ClusterWatcherHookProvider>(
                reg, default_watcher_factory());
        }

        ShellState state{};
        watcher_hook->on_startup(state);
    }

    ~RealMode() {
        ShellState state{};
        if (watcher_hook) watcher_hook->on_exit(state);
        // Persist registry on shutdown so future processes see
        // Running allocations. Best-effort; save() is atomic (tmp +
        // rename) so a crash mid-save leaves the old file intact.
        std::error_code ec;
        std::filesystem::create_directories(registry_path.parent_path(), ec);
        try {
            reg.save(registry_path);
        } catch (const std::exception& e) {
            tash::io::debug(std::string("cluster: registry save failed: ") + e.what());
        }
    }
};

std::unique_ptr<RealMode> g_real;

}  // namespace

bool install_real_engine() {
    uninstall_real_engine();

    auto home = resolve_cluster_home();
    if (!home) return false;

    const auto config_path   = *home / "config.toml";
    const auto registry_path = *home / "registry.json";
    const auto socket_dir    = *home / "sockets";

    if (!std::filesystem::exists(config_path)) return false;

    auto result = ConfigLoader::load(config_path);
    if (auto* err = std::get_if<ConfigError>(&result)) {
        std::cerr << err->format() << "\n";
        return false;
    }

    g_real = std::make_unique<RealMode>(
        std::move(std::get<Config>(result)),
        Registry::load(registry_path),
        socket_dir,
        registry_path);
    set_active_engine(g_real->engine.get());
    return true;
}

void uninstall_real_engine() {
    set_active_engine(nullptr);
    g_real.reset();
}

bool real_engine_installed() { return g_real != nullptr; }

}  // namespace tash::cluster
