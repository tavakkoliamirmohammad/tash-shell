// Tests for TASH_CLUSTER_DEMO wiring.
// Exercises the full up → list → launch → list → attach → down flow
// through dispatch_cluster against the demo engine.

#include <gtest/gtest.h>

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/demo_mode.h"

#include <sstream>

using namespace tash::cluster;

namespace {
std::vector<std::string> argv_of(std::initializer_list<const char*> xs) {
    return {xs.begin(), xs.end()};
}

// Every test starts and ends with no demo engine installed.
class DemoFixture : public ::testing::Test {
protected:
    void SetUp()    override { uninstall_demo_engine(); }
    void TearDown() override { uninstall_demo_engine(); }
};
}  // namespace

TEST_F(DemoFixture, InstallAndUninstallRoundTrips) {
    EXPECT_FALSE(demo_engine_installed());
    EXPECT_EQ(active_engine(), nullptr);

    install_demo_engine();
    EXPECT_TRUE (demo_engine_installed());
    EXPECT_NE(active_engine(), nullptr);

    uninstall_demo_engine();
    EXPECT_FALSE(demo_engine_installed());
    EXPECT_EQ(active_engine(), nullptr);
}

TEST_F(DemoFixture, InstallIsIdempotentAndReplacesRegistryState) {
    // Install, create an allocation via up(), then re-install and verify
    // the new engine's registry is empty (i.e. we got a fresh DemoMode).
    install_demo_engine();
    auto* first = active_engine();
    ASSERT_NE(first, nullptr);
    std::ostringstream out, err;
    ASSERT_EQ(dispatch_cluster({"cluster", "up", "-r", "a100"}, *first, out, err), 0)
        << err.str();

    install_demo_engine();
    auto* second = active_engine();
    ASSERT_NE(second, nullptr);
    out.str(""); err.str("");
    ASSERT_EQ(dispatch_cluster({"cluster", "list"}, *second, out, err), 0);
    EXPECT_NE(out.str().find("no allocations"), std::string::npos) << out.str();
}

TEST_F(DemoFixture, DemoConfigShapeIsSensible) {
    const auto c = demo_config();
    ASSERT_EQ(c.clusters.size(),  1u);
    ASSERT_EQ(c.resources.size(), 1u);
    ASSERT_EQ(c.presets.size(),   1u);
    EXPECT_EQ(c.resources[0].name, "a100");
    EXPECT_EQ(c.resources[0].kind, ResourceKind::Gpu);
    ASSERT_EQ(c.resources[0].routes.size(), 1u);
    EXPECT_EQ(c.resources[0].routes[0].cluster, c.clusters[0].name);
}

TEST_F(DemoFixture, EndToEndUpListLaunchAttachDown) {
    install_demo_engine();
    ASSERT_NE(active_engine(), nullptr);
    auto& eng = *active_engine();

    std::ostringstream out, err;
    int rc;

    // up
    rc = dispatch_cluster(argv_of({"cluster", "up", "-r", "a100", "-t", "1:00:00"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("allocated"), std::string::npos) << out.str();
    const std::string up_out = out.str();

    // list
    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "list"}), eng, out, err);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(out.str().find("demo-cluster:"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("a100"),          std::string::npos);

    // launch
    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "launch", "--workspace", "repoA",
                                     "--preset", "demo-claude"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("launched"), std::string::npos) << out.str();

    // attach
    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "attach", "repoA/1"}), eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();

    // Down uses the jobid produced by the demo engine. The demo's
    // DemoSlurmOps::sbatch starts at 10000 and increments.
    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "down", "demo-cluster:10000", "-y"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("cancelled"), std::string::npos) << out.str();

    // Final list shows nothing.
    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "list"}), eng, out, err);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(out.str().find("no allocations"), std::string::npos) << out.str();
}

TEST_F(DemoFixture, ProbeShowsIdleA100) {
    install_demo_engine();
    auto& eng = *active_engine();

    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "probe", "-r", "a100"}),
                                eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("a100"), std::string::npos) << out.str();
    // Demo reports 4 idle nodes with matching gres.
    EXPECT_NE(out.str().find("4 idle"), std::string::npos);
    EXPECT_NE(out.str().find("4 matching"), std::string::npos);
}

TEST_F(DemoFixture, UnsinstalledThenInstalledThenUninstalledCleanly) {
    EXPECT_FALSE(demo_engine_installed());
    install_demo_engine();
    EXPECT_TRUE(demo_engine_installed());
    uninstall_demo_engine();
    EXPECT_FALSE(demo_engine_installed());

    // Safe to call uninstall_demo_engine twice.
    uninstall_demo_engine();
    EXPECT_FALSE(demo_engine_installed());
}
