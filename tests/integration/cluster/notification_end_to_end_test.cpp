// End-to-end notification test: scenario writes a claude_stopped
// event to a queue; the hook provider's StreamWatcher consumes it;
// apply_event fires the notifier with the expected payload.
//
// Wires together M1.9 (event decoder + apply_event), M3.1 (Notifier
// factory via FakeNotifier), M3.2 (hook provider lifecycle), M3.3
// (StreamWatcher). The only fake surface is the LineSource — every
// other line of code here is production.

#include <gtest/gtest.h>

#include "tash/cluster/registry.h"
#include "tash/cluster/stream_watcher.h"
#include "tash/plugins/cluster_watcher_hook_provider.h"
#include "tash/shell.h"

#include "fakes/fake_notifier.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

// Thread-safe FIFO of "lines" the test can push events into; the
// watcher pulls them out one at a time.
class LineQueue {
public:
    void push(std::string s) {
        std::lock_guard<std::mutex> lk(m_);
        q_.push(std::move(s));
        cv_.notify_one();
    }
    void close() {
        std::lock_guard<std::mutex> lk(m_);
        closed_.store(true, std::memory_order_release);
        cv_.notify_all();
    }
    std::optional<std::string> pop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this]{
            return !q_.empty() || closed_.load(std::memory_order_acquire);
        });
        if (!q_.empty()) {
            std::string s = std::move(q_.front()); q_.pop(); return s;
        }
        return std::nullopt;
    }

private:
    std::mutex                     m_;
    std::condition_variable        cv_;
    std::queue<std::string>        q_;
    std::atomic<bool>              closed_{false};
};

// Pre-populate a registry with one running allocation + workspace +
// instance for the event to target.
void seed_registry(Registry& reg) {
    Allocation a;
    a.id = "utah-notchpeak:42"; a.cluster = "utah-notchpeak";
    a.jobid = "42"; a.node = "notch-n5"; a.state = AllocationState::Running;
    Workspace ws;
    ws.name = "repoA"; ws.cwd = "/scratch/repoA"; ws.tmux_session = "s";
    Instance inst;
    inst.id = "1"; inst.tmux_window = "1"; inst.state = InstanceState::Running;
    ws.instances.push_back(inst);
    a.workspaces.push_back(ws);
    reg.add_allocation(a);
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// E2E: watcher → apply_event → notifier, fanned through the hook provider
// ══════════════════════════════════════════════════════════════════════════════

TEST(NotificationEndToEnd, ClaudeStoppedEventFiresNotifier) {
    Registry      reg;
    FakeNotifier  notify;
    LineQueue     queue;
    seed_registry(reg);

    // Factory: one StreamWatcher per Running allocation, reading from
    // the shared queue. In real life each allocation has its own source;
    // for this E2E we only seeded one allocation so a shared queue is fine.
    WatcherFactory factory =
        [&queue, &notify](const Allocation&, Registry& r) -> std::unique_ptr<IWatcher> {
            return std::make_unique<StreamWatcher>(
                [&queue]() { return queue.pop(); },
                r, notify);
        };

    ClusterWatcherHookProvider provider(reg, factory);
    ShellState state{};
    provider.on_startup(state);

    // Scenario timeline: Claude Code stops and writes an event file
    // on the cluster; our ssh tail -F (modelled here as queue.push)
    // hands the line to the watcher.
    queue.push(R"({"ts":"2026-04-19T10:00:00Z","instance":"repoA/1","kind":"stopped","detail":"awaiting input"})");

    // Poll for the notification. Desktop + bell both fire per event.
    for (int i = 0; i < 200 && notify.desktop_calls.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }

    ASSERT_EQ(notify.desktop_calls.size(), 1u);
    EXPECT_EQ(notify.bell_count, 1);
    EXPECT_NE(notify.desktop_calls[0].title.find("attention"),     std::string::npos)
        << notify.desktop_calls[0].title;
    EXPECT_NE(notify.desktop_calls[0].body.find("utah-notchpeak"),  std::string::npos)
        << notify.desktop_calls[0].body;
    EXPECT_NE(notify.desktop_calls[0].body.find("repoA/1"),         std::string::npos);
    EXPECT_NE(notify.desktop_calls[0].body.find("awaiting input"),  std::string::npos);

    // Registry reflects the stop.
    auto* a = reg.find_allocation("utah-notchpeak:42");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->workspaces[0].instances[0].state, InstanceState::Stopped);
    EXPECT_EQ(a->workspaces[0].instances[0].last_event_at, "2026-04-19T10:00:00Z");

    queue.close();           // let the watcher's source return nullopt -> run() exits
    provider.on_exit(state); // join cleanly
}

// Dedup: two identical event lines only fire once.

TEST(NotificationEndToEnd, DuplicateEventsAreDedupedAcrossWatcher) {
    Registry reg;
    FakeNotifier notify;
    LineQueue queue;
    seed_registry(reg);

    WatcherFactory factory =
        [&queue, &notify](const Allocation&, Registry& r) -> std::unique_ptr<IWatcher> {
            return std::make_unique<StreamWatcher>(
                [&queue]() { return queue.pop(); },
                r, notify);
        };

    ClusterWatcherHookProvider provider(reg, factory);
    ShellState state{};
    provider.on_startup(state);

    const std::string same_event =
        R"({"ts":"2026-04-19T10:00:00Z","instance":"repoA/1","kind":"stopped"})";
    queue.push(same_event);
    queue.push(same_event);

    for (int i = 0; i < 200 && notify.desktop_calls.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    // Wait a bit more to be sure the second copy had time to be read + dropped.
    std::this_thread::sleep_for(std::chrono::milliseconds{30});

    EXPECT_EQ(notify.desktop_calls.size(), 1u);
    EXPECT_EQ(notify.bell_count, 1);

    queue.close();
    provider.on_exit(state);
}

// Malformed lines don't break the stream or fire false-positive events.

TEST(NotificationEndToEnd, MalformedJsonLinesAreSkipped) {
    Registry reg;
    FakeNotifier notify;
    LineQueue queue;
    seed_registry(reg);

    WatcherFactory factory =
        [&queue, &notify](const Allocation&, Registry& r) -> std::unique_ptr<IWatcher> {
            return std::make_unique<StreamWatcher>(
                [&queue]() { return queue.pop(); },
                r, notify);
        };

    ClusterWatcherHookProvider provider(reg, factory);
    ShellState state{};
    provider.on_startup(state);

    queue.push("this is not json");
    queue.push("{\"partial\":");    // truncated
    queue.push(R"({"ts":"t","instance":"repoA/1","kind":"idle"})");   // valid

    for (int i = 0; i < 200 && notify.desktop_calls.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    EXPECT_EQ(notify.desktop_calls.size(), 1u);

    queue.close();
    provider.on_exit(state);
}
