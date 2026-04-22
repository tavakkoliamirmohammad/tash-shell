// Tests for TmuxFallbackDetector — synthesises window_exited +
// silence_threshold events from successive `tmux list-windows`
// snapshots. Caller-provided timestamps (no real sleep).

#include <gtest/gtest.h>

#include "tash/cluster/watcher.h"

#include <chrono>

using namespace tash::cluster;

namespace {
auto t0 = std::chrono::steady_clock::time_point{};
auto at_seconds(int s) { return t0 + std::chrono::seconds{s}; }
}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Window-death detection
// ══════════════════════════════════════════════════════════════════════════════

TEST(TmuxFallbackDetector, FirstSnapshotProducesNoEvents) {
    TmuxFallbackDetector d;
    const std::vector<WindowSnapshot> snap = {
        {"s1", "w1", 111},
        {"s1", "w2", 222},
    };
    const auto ev = d.observe("repoA", snap, at_seconds(0), "t0");
    EXPECT_EQ(ev.size(), 0u);
}

TEST(TmuxFallbackDetector, VanishedWindowEmitsWindowExited) {
    TmuxFallbackDetector d;
    d.observe("repoA", {{"s1", "w1", 111}, {"s1", "w2", 222}},
               at_seconds(0), "t0");
    const auto ev = d.observe("repoA", {{"s1", "w2", 222}}, at_seconds(1), "t1");
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].kind,      "window_exited");
    EXPECT_EQ(ev[0].workspace, "repoA");
    EXPECT_EQ(ev[0].instance,  "w1");
    EXPECT_EQ(ev[0].ts,        "t1");
}

TEST(TmuxFallbackDetector, VanishedWindowNotReemittedIfStillGone) {
    TmuxFallbackDetector d;
    d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(0), "t0");
    const auto ev1 = d.observe("repoA", {},                       at_seconds(1), "t1");
    EXPECT_EQ(ev1.size(), 1u);
    const auto ev2 = d.observe("repoA", {},                       at_seconds(2), "t2");
    EXPECT_EQ(ev2.size(), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// Silence-threshold detection
// ══════════════════════════════════════════════════════════════════════════════

TEST(TmuxFallbackDetector, StableWindowEmitsSilenceAfterThreshold) {
    TmuxFallbackDetector d; d.silence_threshold = std::chrono::seconds{60};
    // First seen at t=0.
    d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(0), "t0");
    // At t=30, still under threshold — no event.
    const auto early = d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(30), "t30");
    EXPECT_EQ(early.size(), 0u);
    // At t=61, over threshold — silence_threshold emitted once.
    const auto late  = d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(61), "t61");
    ASSERT_EQ(late.size(), 1u);
    EXPECT_EQ(late[0].kind,      "silence_threshold");
    EXPECT_EQ(late[0].workspace, "repoA");
    EXPECT_EQ(late[0].instance,  "w1");
    EXPECT_EQ(late[0].ts,        "t61");
}

TEST(TmuxFallbackDetector, SilenceDedupOnSameStreak) {
    TmuxFallbackDetector d; d.silence_threshold = std::chrono::seconds{10};
    d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(0),  "t0");
    const auto once = d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(20), "t20");
    EXPECT_EQ(once.size(), 1u);
    const auto dup  = d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(40), "t40");
    EXPECT_EQ(dup.size(), 0u);
}

TEST(TmuxFallbackDetector, PidChangeResetsSilenceStreak) {
    TmuxFallbackDetector d; d.silence_threshold = std::chrono::seconds{10};
    d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(0),  "t0");
    const auto once = d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(20), "t20");
    EXPECT_EQ(once.size(), 1u);

    // Pid changes — silence streak reset.
    const auto after_change = d.observe("repoA", {{"s1", "w1", 222}}, at_seconds(25), "t25");
    EXPECT_EQ(after_change.size(), 0u);

    // Still inside the new threshold → no event.
    const auto midway = d.observe("repoA", {{"s1", "w1", 222}}, at_seconds(30), "t30");
    EXPECT_EQ(midway.size(), 0u);

    // Past new threshold → emit once again.
    const auto again  = d.observe("repoA", {{"s1", "w1", 222}}, at_seconds(45), "t45");
    ASSERT_EQ(again.size(), 1u);
    EXPECT_EQ(again[0].kind, "silence_threshold");
}

TEST(TmuxFallbackDetector, MultipleWindowsIndependentlyTracked) {
    TmuxFallbackDetector d; d.silence_threshold = std::chrono::seconds{10};
    d.observe("repoA", {{"s1", "w1", 111}, {"s1", "w2", 222}}, at_seconds(0),  "t0");

    // w1 silence tripped at t=15; w2 still fresh (same snapshot time).
    // Wait — both are first-seen at 0, so both trip at 15. Let's assert
    // both events land.
    const auto ev = d.observe("repoA", {{"s1", "w1", 111}, {"s1", "w2", 222}},
                                at_seconds(15), "t15");
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].kind, "silence_threshold");
    EXPECT_EQ(ev[1].kind, "silence_threshold");
}

TEST(TmuxFallbackDetector, WindowBothVanishesAndWasSilentOnlyEmitsExitOnce) {
    TmuxFallbackDetector d; d.silence_threshold = std::chrono::seconds{10};
    d.observe("repoA", {{"s1", "w1", 111}}, at_seconds(0), "t0");

    // Past silence threshold AND window gone at the same poll. Only
    // window_exited is appropriate — silence is moot if the window
    // is already dead.
    const auto ev = d.observe("repoA", {}, at_seconds(20), "t20");
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].kind, "window_exited");
}
