// Help + error-format audit for the `cluster` builtin.
//
// Every subcommand must:
//   - respond to --help with exit 0 and a usage block
//   - mention its own name in the help output
//   - print user-visible errors prefixed with "tash: cluster: "
//
// Snapshot-ish: we don't byte-compare, we assert the output contains the
// structural markers that matter. That keeps the tests stable when we
// re-word individual lines but breaks fast if the shape regresses.

#include <gtest/gtest.h>

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"

#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

#include <sstream>
#include <string>
#include <vector>

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

struct Harness {
    Config         cfg;
    Registry       reg;
    FakeSshClient  ssh;
    FakeSlurmOps   slurm;
    FakeTmuxOps    tmux;
    FakeNotifier   notify;
    FakePrompt     prompt;
    FakeClock      clock;
    ClusterEngine  engine{cfg, reg, ssh, slurm, tmux, notify, prompt, clock};
};

std::vector<std::string> argv_of(std::initializer_list<const char*> xs) {
    return {xs.begin(), xs.end()};
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Top-level help
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterHelp, TopLevelListsEverySubcommand) {
    Harness h;
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "--help"}),
                                       h.engine, out, err);
    EXPECT_EQ(rc, 0);

    const std::string s = out.str();
    for (const auto& sub : {"up", "launch", "attach", "list", "down",
                              "kill", "sync", "probe", "import", "doctor",
                              "help"}) {
        EXPECT_TRUE(contains(s, sub)) << "missing subcommand in help: " << sub;
    }
    EXPECT_TRUE(contains(s, "usage: cluster")) << s;
}

TEST(ClusterHelp, NoArgsPrintsTopLevelHelp) {
    Harness h;
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster"}), h.engine, out, err);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(contains(out.str(), "subcommands"));
}

TEST(ClusterHelp, HelpKeywordWithSubcommandRouts) {
    Harness h;
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "help", "launch"}),
                                       h.engine, out, err);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(contains(out.str(), "cluster launch"));
}

// ══════════════════════════════════════════════════════════════════════════════
// Per-subcommand --help
// ══════════════════════════════════════════════════════════════════════════════

namespace {
struct SubcommandExpectation {
    const char* sub;
    std::vector<const char*> musts;    // strings that must appear
};

const SubcommandExpectation kExpect[] = {
    { "up",     { "cluster up",     "usage:", "--resource" } },
    { "launch", { "cluster launch", "usage:", "--workspace", "--preset" } },
    { "attach", { "cluster attach", "usage:", "workspace" } },
    { "list",   { "cluster list",   "usage:" } },
    { "down",   { "cluster down",   "usage:", "allocation" } },
    { "kill",   { "cluster kill",   "usage:" } },
    { "sync",   { "cluster sync",   "usage:" } },
    { "probe",  { "cluster probe",  "usage:", "--resource" } },
    { "import", { "cluster import", "usage:", "--via" } },
    { "doctor", { "cluster doctor", "usage:" } },
};
}  // namespace

TEST(ClusterHelp, EverySubcommandHasUsefulHelp) {
    for (const auto& e : kExpect) {
        Harness h;
        std::ostringstream out, err;
        const int rc = dispatch_cluster(argv_of({"cluster", e.sub, "--help"}),
                                           h.engine, out, err);
        EXPECT_EQ(rc, 0) << e.sub;
        const std::string s = out.str();
        for (const char* m : e.musts) {
            EXPECT_TRUE(contains(s, m))
                << "cluster " << e.sub << " --help missing: \"" << m << "\"\n"
                << "full output:\n" << s;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Error audit — every user-visible error line starts with "tash: cluster: "
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterHelp, UnknownSubcommandErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "reticulate"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: ")) << err.str();
    EXPECT_TRUE(contains(err.str(), "reticulate"));
}

TEST(ClusterHelp, UpMissingResourceErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "up"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
    EXPECT_TRUE(contains(err.str(), "-r"));
}

TEST(ClusterHelp, LaunchMissingWorkspaceErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "launch"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
    EXPECT_TRUE(contains(err.str(), "workspace"));
}

TEST(ClusterHelp, AttachMissingPositionalErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "attach"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
}

TEST(ClusterHelp, DownMissingAllocIdErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "down"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
    EXPECT_TRUE(contains(err.str(), "allocation-id"));
}

TEST(ClusterHelp, KillMissingPositionalErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "kill"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
}

TEST(ClusterHelp, ProbeMissingResourceErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "probe"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
}

TEST(ClusterHelp, ImportMissingViaErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "import", "777"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
    EXPECT_TRUE(contains(err.str(), "--via"));
}

TEST(ClusterHelp, UpUnknownOptionErrorFormat) {
    Harness h;
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "up", "--bogus"}), h.engine, out, err);
    EXPECT_TRUE(contains(err.str(), "tash: cluster: "));
    EXPECT_TRUE(contains(err.str(), "unknown option"));
}
