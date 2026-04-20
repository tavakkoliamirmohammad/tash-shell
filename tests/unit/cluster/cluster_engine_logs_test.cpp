// ClusterEngine::logs — targeted fetch of stop-hook events.

#include <gtest/gtest.h>

#include "tash/cluster/cluster_engine.h"

#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

struct Harness {
    Config cfg;
    Registry reg;
    FakeSshClient ssh; FakeSlurmOps slurm; FakeTmuxOps tmux;
    FakeNotifier notify; FakePrompt prompt; FakeClock clock;
    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

Allocation alloc_with_workspace(const std::string& cluster,
                                 const std::string& jobid,
                                 const std::string& ws_name) {
    Allocation a;
    a.id = cluster + ":" + jobid;
    a.cluster = cluster;
    a.jobid = jobid;
    a.state = AllocationState::Running;
    Workspace w;
    w.name = ws_name;
    w.cwd = "/scratch/" + ws_name;
    w.tmux_session = "tash-" + cluster + "-" + jobid + "-" + ws_name;
    a.workspaces.push_back(std::move(w));
    return a;
}

}  // namespace

TEST(ClusterEngineLogs, MissingWorkspaceFieldRejected) {
    Harness h;
    LogsSpec spec;   // workspace empty
    auto r = h.engine().logs(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("workspace"), std::string::npos);
}

TEST(ClusterEngineLogs, UnknownWorkspaceYieldsHelpfulError) {
    Harness h;
    h.reg.add_allocation(alloc_with_workspace("c1", "100", "repoA"));

    LogsSpec spec; spec.workspace = "noSuchWs";
    auto r = h.engine().logs(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("noSuchWs"), std::string::npos);
}

TEST(ClusterEngineLogs, AmbiguousWorkspaceRequiresAllocFlag) {
    Harness h;
    h.reg.add_allocation(alloc_with_workspace("c1", "100", "repoA"));
    h.reg.add_allocation(alloc_with_workspace("c2", "200", "repoA"));

    LogsSpec spec; spec.workspace = "repoA";
    auto r = h.engine().logs(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("ambiguous"), std::string::npos);
    EXPECT_NE(err->message.find("--alloc"),   std::string::npos);
}

TEST(ClusterEngineLogs, HappyPathIssuesShCatOverSsh) {
    Harness h;
    h.reg.add_allocation(alloc_with_workspace("c1", "100", "repoA"));

    h.ssh.queue_run(SshResult{0,
        "==> /home/u/.tash-cluster/events/repoA/1.event <==\n"
        "{\"ts\":\"2026-04-20T...\",\"kind\":\"stopped\"}\n",
        ""});

    LogsSpec spec; spec.workspace = "repoA"; spec.tail_lines = 50;
    auto r = h.engine().logs(spec);
    auto* rep = std::get_if<ClusterEngine::LogsReport>(&r);
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->cluster, "c1");
    EXPECT_EQ(rep->alloc_id, "c1:100");
    EXPECT_NE(rep->contents.find("stopped"), std::string::npos);

    // Confirm we shelled through sh -c with the expected fragments.
    ASSERT_EQ(h.ssh.run_calls.size(), 1u);
    const auto& argv = h.ssh.run_calls[0].argv;
    ASSERT_GE(argv.size(), 3u);
    EXPECT_EQ(argv[0], "/bin/sh");
    EXPECT_EQ(argv[1], "-c");
    EXPECT_NE(argv[2].find("tail -n 50"),    std::string::npos) << argv[2];
    EXPECT_NE(argv[2].find("events/repoA"),   std::string::npos);
}

TEST(ClusterEngineLogs, SpecificInstanceNarrowsGlob) {
    Harness h;
    h.reg.add_allocation(alloc_with_workspace("c1", "100", "repoA"));
    h.ssh.queue_run(SshResult{0, "", ""});

    LogsSpec spec; spec.workspace = "repoA"; spec.instance = "feature-x";
    (void)h.engine().logs(spec);

    ASSERT_EQ(h.ssh.run_calls.size(), 1u);
    const auto& cmd = h.ssh.run_calls[0].argv[2];
    EXPECT_NE(cmd.find("feature-x.event"), std::string::npos) << cmd;
    EXPECT_EQ(cmd.find("/*.event"),        std::string::npos) << cmd;
}

TEST(ClusterEngineLogs, SshNonZeroExitBecomesEngineError) {
    Harness h;
    h.reg.add_allocation(alloc_with_workspace("c1", "100", "repoA"));
    h.ssh.queue_run(SshResult{255, "", ""});

    LogsSpec spec; spec.workspace = "repoA";
    auto r = h.engine().logs(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("ssh"), std::string::npos);
}

TEST(ClusterEngineLogs, EmptyOutputReportedGracefully) {
    Harness h;
    h.reg.add_allocation(alloc_with_workspace("c1", "100", "repoA"));
    h.ssh.queue_run(SshResult{0, "", ""});   // out empty

    LogsSpec spec; spec.workspace = "repoA";
    auto r = h.engine().logs(spec);
    auto* rep = std::get_if<ClusterEngine::LogsReport>(&r);
    ASSERT_NE(rep, nullptr);
    EXPECT_NE(rep->contents.find("no events yet"), std::string::npos);
}
