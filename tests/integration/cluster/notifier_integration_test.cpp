// Integration tests for the platform notifier factory. Uses the
// tests/fakes/bin/osascript + notify-send stubs to capture what
// argv the real MacNotifier / LinuxNotifier send.
//
// Test exposes the bypass (MAKE_MAC_NOTIFIER / MAKE_LINUX_NOTIFIER)
// so both impls are exercised on any host — we don't care what the
// factory *would* pick for the test runner, just that each platform
// impl produces the correct argv.

#include "integration_fixture.h"

#include "tash/cluster/notifier.h"
#include "tash/cluster/notifier_factory.h"

#include <memory>

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace tash::cluster {
// Exposed by src/cluster/notifier_factory.cpp for cross-platform testing.
std::unique_ptr<INotifier> make_mac_notifier_for_testing();
std::unique_ptr<INotifier> make_linux_notifier_for_testing();
}

class NotifierIntegrationFixture : public IntegrationFixture {};

// ── macOS impl: osascript ─────────────────────────────────────

TEST_F(NotifierIntegrationFixture, MacNotifierSpawnsOsascriptWithTitleAndBody) {
    auto n = make_mac_notifier_for_testing();
    n->desktop("Claude needs attention",
                 "utah-notchpeak · repoA/feature-x — awaiting input");
    const auto log = read_log();
    EXPECT_NE(log.find("[osascript]"),                        std::string::npos) << log;
    EXPECT_NE(log.find("display notification"),               std::string::npos) << log;
    EXPECT_NE(log.find("awaiting input"),                     std::string::npos);
    EXPECT_NE(log.find("Claude needs attention"),             std::string::npos);
}

TEST_F(NotifierIntegrationFixture, MacNotifierEscapesAppleScriptQuotes) {
    auto n = make_mac_notifier_for_testing();
    n->desktop("title", R"(body with "quotes" and \slashes)");
    const auto log = read_log();
    EXPECT_NE(log.find("[osascript]"), std::string::npos) << log;
    // AppleScript escaping: " -> \", \ -> \\ — the sanity check is the
    // stub got exactly the escaped form, so the stub argv contains \".
    EXPECT_NE(log.find("\\\""), std::string::npos) << log;
}

// ── Linux impl: notify-send ──────────────────────────────────

TEST_F(NotifierIntegrationFixture, LinuxNotifierSpawnsNotifySendWithTitleAndBody) {
    auto n = make_linux_notifier_for_testing();
    n->desktop("Instance exited immediately",
                 "utah · repoA/1 — command exited right after launch");
    const auto log = read_log();
    EXPECT_NE(log.find("[notify-send]"),                         std::string::npos) << log;
    EXPECT_NE(log.find("Instance exited immediately"),           std::string::npos);
    EXPECT_NE(log.find("command exited right after launch"),     std::string::npos);
}

// ── Factory returns *something* on every platform ───────────

TEST_F(NotifierIntegrationFixture, FactoryReturnsNonNullNotifier) {
    auto n = make_notifier();
    ASSERT_NE(n, nullptr);
    // Calling desktop / bell on whatever the factory returned must not
    // throw or crash on any platform.
    n->desktop("tash", "smoke");
    n->bell();
}
