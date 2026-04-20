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

// Stdin-backed prompt. Reads one line, returns its first char if it's
// in `choices`; otherwise returns '\0' (engine treats as non-
// interactive fall-through).
class StdinPrompt : public IPrompt {
public:
    char choice(const std::string& message,
                const std::string& choices) override {
        std::cerr << message << std::flush;
        std::string line;
        if (!std::getline(std::cin, line) || line.empty()) return '\0';
        const char c = line.front();
        return (choices.find(c) != std::string::npos) ? c : '\0';
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

        // Watcher hook: use the no-op factory for now. The real
        // ssh-tail watcher (make_ssh_tail_watcher_factory) exists
        // but is opt-in; wiring it unconditionally would spawn a
        // background `ssh tail -F` per Running allocation the first
        // time a user opens a tash shell, which is too much surprise
        // for v1 real-cluster enablement.
        watcher_hook = std::make_unique<ClusterWatcherHookProvider>(
            reg, default_watcher_factory());

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
