// Shared helper for building a real-seams ClusterEngine inside an
// IntegrationFixture. Every integration test in this dir uses this.

#ifndef TASH_CLUSTER_TEST_INTEGRATION_ENGINE_HELPER_H
#define TASH_CLUSTER_TEST_INTEGRATION_ENGINE_HELPER_H

#include "integration_fixture.h"

#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"
#include "tash/cluster/notifier.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/slurm_ops.h"
#include "tash/cluster/ssh_client.h"
#include "tash/cluster/tmux_ops.h"

#include <memory>

namespace tash::cluster::testing {

class SilentNotifier : public INotifier {
public:
    void desktop(const std::string&, const std::string&) override {}
    void bell() override {}
};
class KeepPrompt : public IPrompt {
public:
    char choice(const std::string&, const std::string&) override { return 'k'; }
};

inline Config one_route_a100_config() {
    Config c;
    c.defaults.workspace_base = "/tmp";
    c.defaults.default_preset = "claude";
    c.clusters.push_back({"c1", "stub-host", ""});
    Resource r;
    r.name = "a100"; r.kind = ResourceKind::Gpu;
    r.routes.push_back({"c1", "acc", "p", "q", "gpu:a100:1"});
    c.resources.push_back(r);
    Preset p; p.name = "claude"; p.command = "claude";
    c.presets.push_back(p);
    return c;
}

// Helper fixture — cfg + reg + engine ready to drive from each test.
class EngineIntegrationFixture : public IntegrationFixture {
protected:
    Config          cfg{one_route_a100_config()};
    Registry        reg;
    std::unique_ptr<ISshClient>  ssh;
    std::unique_ptr<ISlurmOps>   slurm;
    std::unique_ptr<ITmuxOps>    tmux;
    SilentNotifier  notify;
    KeepPrompt      prompt;
    RealClock       clock;

    void SetUp() override {
        IntegrationFixture::SetUp();
        ssh = make_ssh_client(
            [this](const std::string& name) {
                for (const auto& c : cfg.clusters) {
                    if (c.name == name) return c.ssh_host;
                }
                return name;
            },
            tmp_dir / "sockets");
        slurm = make_slurm_ops();
        tmux  = make_tmux_ops();
    }

    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, *ssh, *slurm, *tmux, notify, prompt, clock);
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_TEST_INTEGRATION_ENGINE_HELPER_H
