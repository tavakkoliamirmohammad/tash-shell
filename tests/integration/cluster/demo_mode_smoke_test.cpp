// Tier-2: demo mode, driven through the builtin dispatcher as the
// user would. Proves TASH_CLUSTER_DEMO=1 + `cluster up -r a100` is
// fully self-contained (no PATH overrides needed — the demo's own
// in-memory seams handle everything).

#include <gtest/gtest.h>

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/demo_mode.h"

#include <sstream>
#include <vector>

using namespace tash::cluster;

namespace {
std::vector<std::string> argv_of(std::initializer_list<const char*> xs) {
    return {xs.begin(), xs.end()};
}
}  // namespace

class DemoModeIntegrationFixture : public ::testing::Test {
protected:
    void SetUp()    override { uninstall_demo_engine(); }
    void TearDown() override { uninstall_demo_engine(); }
};

TEST_F(DemoModeIntegrationFixture, FullUserFlow) {
    install_demo_engine();
    ASSERT_NE(active_engine(), nullptr);
    auto& eng = *active_engine();

    std::ostringstream out, err;
    int rc;

    rc = dispatch_cluster(argv_of({"cluster", "up", "-r", "a100"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("allocated"), std::string::npos);

    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "launch", "--workspace", "wsA",
                                     "--cmd", "true"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("launched"), std::string::npos);

    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "list"}), eng, out, err);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(out.str().find("demo-cluster:"), std::string::npos);

    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "probe", "-r", "a100"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(out.str().find("4 matching"), std::string::npos) << out.str();

    out.str(""); err.str("");
    rc = dispatch_cluster(argv_of({"cluster", "down", "demo-cluster:10000", "-y"}),
                           eng, out, err);
    ASSERT_EQ(rc, 0) << err.str();
}
