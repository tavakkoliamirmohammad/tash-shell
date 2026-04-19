// Tests for tash::cluster::watcher — EventDecoder, EventDedup,
// state_for_kind, apply_event, Backoff, StopToken.
//
// Pure-logic coverage; no ssh, no threads, no real tail -F.

#include <gtest/gtest.h>

#include "tash/cluster/watcher.h"
#include "fakes/fake_notifier.h"

#include <chrono>

using namespace tash::cluster;
using namespace tash::cluster::testing;

// ══════════════════════════════════════════════════════════════════════════════
// EventDecoder
// ══════════════════════════════════════════════════════════════════════════════

TEST(EventDecoder, DecodesValidJsonEvent) {
    const std::string line = R"({"ts":"2026-04-18T10:00:00Z","instance":"repoA/feature-x","kind":"stopped","detail":"awaiting input"})";
    auto e = EventDecoder::decode(line);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->workspace, "repoA");
    EXPECT_EQ(e->instance,  "feature-x");
    EXPECT_EQ(e->kind,      "stopped");
    EXPECT_EQ(e->ts,        "2026-04-18T10:00:00Z");
    EXPECT_EQ(e->detail,    "awaiting input");
}

TEST(EventDecoder, DecodeHandlesMissingDetail) {
    const std::string line = R"({"ts":"2026-04-18T10:00:00Z","instance":"w/i","kind":"idle"})";
    auto e = EventDecoder::decode(line);
    ASSERT_TRUE(e.has_value());
    EXPECT_TRUE(e->detail.empty());
}

TEST(EventDecoder, RejectsMalformedJson) {
    EXPECT_FALSE(EventDecoder::decode("not json at all").has_value());
    EXPECT_FALSE(EventDecoder::decode("{").has_value());
    EXPECT_FALSE(EventDecoder::decode("").has_value());
}

TEST(EventDecoder, RejectsEventMissingRequiredFields) {
    // Missing ts
    EXPECT_FALSE(EventDecoder::decode(R"({"instance":"w/i","kind":"stopped"})").has_value());
    // Missing instance
    EXPECT_FALSE(EventDecoder::decode(R"({"ts":"t","kind":"stopped"})").has_value());
    // Missing kind
    EXPECT_FALSE(EventDecoder::decode(R"({"ts":"t","instance":"w/i"})").has_value());
}

TEST(EventDecoder, RejectsInstanceWithoutWorkspaceSlash) {
    EXPECT_FALSE(EventDecoder::decode(
        R"({"ts":"t","instance":"solo","kind":"stopped"})").has_value());
}

// ══════════════════════════════════════════════════════════════════════════════
// state_for_kind
// ══════════════════════════════════════════════════════════════════════════════

TEST(EventKindMapping, KnownKindsMapCorrectly) {
    EXPECT_EQ(state_for_kind("stopped").value(),     InstanceState::Stopped);
    EXPECT_EQ(state_for_kind("idle").value(),        InstanceState::Idle);
    EXPECT_EQ(state_for_kind("crashed").value(),     InstanceState::Crashed);
    EXPECT_EQ(state_for_kind("window_exited").value(), InstanceState::Exited);
    EXPECT_EQ(state_for_kind("silence_threshold").value(), InstanceState::Idle);
}

TEST(EventKindMapping, UnknownKindReturnsNullopt) {
    EXPECT_FALSE(state_for_kind("reticulated").has_value());
    EXPECT_FALSE(state_for_kind("").has_value());
}

// ══════════════════════════════════════════════════════════════════════════════
// EventDedup
// ══════════════════════════════════════════════════════════════════════════════

TEST(EventDedup, AdmitsOnceRejectsSame) {
    EventDedup d;
    Event e{"w", "i", "stopped", "t1", ""};
    EXPECT_TRUE (d.admit(e));
    EXPECT_FALSE(d.admit(e));
    EXPECT_EQ(d.size(), 1u);
}

TEST(EventDedup, DifferentTsAllowsBoth) {
    EventDedup d;
    EXPECT_TRUE(d.admit(Event{"w", "i", "stopped", "t1", ""}));
    EXPECT_TRUE(d.admit(Event{"w", "i", "stopped", "t2", ""}));
    EXPECT_EQ(d.size(), 2u);
}

TEST(EventDedup, DifferentKindSameTsAllowsBoth) {
    EventDedup d;
    EXPECT_TRUE(d.admit(Event{"w", "i", "stopped", "t1", ""}));
    EXPECT_TRUE(d.admit(Event{"w", "i", "idle",    "t1", ""}));
}

TEST(EventDedup, ClearResets) {
    EventDedup d;
    d.admit(Event{"w", "i", "stopped", "t1", ""});
    d.clear();
    EXPECT_EQ(d.size(), 0u);
    EXPECT_TRUE(d.admit(Event{"w", "i", "stopped", "t1", ""}));
}

// ══════════════════════════════════════════════════════════════════════════════
// apply_event — integrates with Registry and a notifier fake
// ══════════════════════════════════════════════════════════════════════════════

namespace {

Registry registry_with_one_instance(std::optional<std::string> inst_name = {}) {
    Registry r;
    Allocation a;
    a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100"; a.node = "n1";
    a.state = AllocationState::Running;
    Workspace ws;
    ws.name = "repoA"; ws.cwd = "/s/repoA"; ws.tmux_session = "sess";
    Instance inst;
    inst.id = "1"; inst.tmux_window = inst_name.value_or("1");
    inst.name = inst_name;
    inst.state = InstanceState::Running;
    ws.instances.push_back(inst);
    a.workspaces.push_back(ws);
    r.add_allocation(a);
    return r;
}

}  // namespace

TEST(ApplyEvent, UpdatesStateAndFiresNotification) {
    Registry r = registry_with_one_instance();
    FakeNotifier n;
    Event e{"repoA", "1", "stopped", "2026-04-18T10:00:00Z", "awaiting input"};

    EXPECT_TRUE(apply_event(e, r, n));

    auto* a = r.find_allocation("c1:100");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->workspaces[0].instances[0].state,         InstanceState::Stopped);
    EXPECT_EQ(a->workspaces[0].instances[0].last_event_at, "2026-04-18T10:00:00Z");
    EXPECT_EQ(n.desktop_calls.size(), 1u);
    EXPECT_EQ(n.bell_count, 1);
    // Body should mention the instance path + detail
    EXPECT_NE(n.desktop_calls[0].body.find("repoA/1"),         std::string::npos);
    EXPECT_NE(n.desktop_calls[0].body.find("awaiting input"),  std::string::npos);
}

TEST(ApplyEvent, ResolvesInstanceByName) {
    Registry r = registry_with_one_instance("feature-x");
    FakeNotifier n;
    Event e{"repoA", "feature-x", "window_exited", "ts", "exited cleanly"};

    EXPECT_TRUE(apply_event(e, r, n));
    auto* a = r.find_allocation("c1:100");
    EXPECT_EQ(a->workspaces[0].instances[0].state, InstanceState::Exited);
}

TEST(ApplyEvent, NoMatchReturnsFalse) {
    Registry r = registry_with_one_instance();
    FakeNotifier n;
    Event e{"wrongws", "1", "stopped", "t", ""};
    EXPECT_FALSE(apply_event(e, r, n));
    EXPECT_EQ(n.desktop_calls.size(), 0u);
    EXPECT_EQ(n.bell_count, 0);
}

TEST(ApplyEvent, UnknownKindStillUpdatesLastEventButNoStateChange) {
    Registry r = registry_with_one_instance();
    FakeNotifier n;
    Event e{"repoA", "1", "reticulated", "t", ""};

    EXPECT_TRUE(apply_event(e, r, n));
    auto* a = r.find_allocation("c1:100");
    EXPECT_EQ(a->workspaces[0].instances[0].state, InstanceState::Running); // unchanged
    EXPECT_EQ(a->workspaces[0].instances[0].last_event_at, "t");
    EXPECT_EQ(n.desktop_calls.size(), 1u);   // still notify: "something happened"
}

// ══════════════════════════════════════════════════════════════════════════════
// Backoff
// ══════════════════════════════════════════════════════════════════════════════

TEST(Backoff, SequenceDoublesUpToCap) {
    Backoff b(std::chrono::milliseconds{1000}, std::chrono::milliseconds{30000});
    EXPECT_EQ(b.next().count(), 1000);
    EXPECT_EQ(b.next().count(), 2000);
    EXPECT_EQ(b.next().count(), 4000);
    EXPECT_EQ(b.next().count(), 8000);
    EXPECT_EQ(b.next().count(), 16000);
    EXPECT_EQ(b.next().count(), 30000);   // capped
    EXPECT_EQ(b.next().count(), 30000);   // stays at cap
}

TEST(Backoff, ResetGoesBackToBase) {
    Backoff b;
    b.next(); b.next(); b.next();
    b.reset();
    EXPECT_EQ(b.next().count(), 1000);
    EXPECT_EQ(b.total().count(), 1000);
}

TEST(Backoff, AbandonsAfterThreeMinutes) {
    Backoff b;
    EXPECT_FALSE(b.should_abandon());
    // Burn many iterations to accumulate > 3 minutes.
    for (int i = 0; i < 20; ++i) b.next();
    EXPECT_TRUE(b.should_abandon());
}

TEST(Backoff, CustomAbandonThreshold) {
    Backoff b;
    b.next(); b.next();   // 3s total
    EXPECT_TRUE (b.should_abandon(std::chrono::milliseconds{2000}));
    EXPECT_FALSE(b.should_abandon(std::chrono::seconds{60}));
}

// ══════════════════════════════════════════════════════════════════════════════
// StopToken
// ══════════════════════════════════════════════════════════════════════════════

TEST(StopToken, StartsUnstopped) {
    StopToken t;
    EXPECT_FALSE(t.stopped());
}

TEST(StopToken, RequestStopIsObservable) {
    StopToken t;
    t.request_stop();
    EXPECT_TRUE(t.stopped());
}

TEST(StopToken, ResetClears) {
    StopToken t;
    t.request_stop();
    t.reset();
    EXPECT_FALSE(t.stopped());
}
