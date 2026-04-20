// Tests for ClusterWatcherHookProvider lifecycle.
//
// Strategy: inject a test WatcherFactory that returns FakeWatchers
// (tiny RAII-ish classes that record start/stop and block on an
// atomic flag). Assert the provider spawns exactly one thread per
// Running allocation and cleanly joins them all on on_exit.

#include <gtest/gtest.h>

#include "tash/plugins/cluster_watcher_hook_provider.h"
#include "tash/cluster/notifier.h"
#include "tash/cluster/registry.h"
#include "tash/shell.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace tash::cluster;

namespace {

class FakeWatcher : public IWatcher {
public:
    std::atomic<bool>* start_counter;  // shared across all fakes
    std::atomic<bool>  stop_requested{false};
    std::atomic<bool>  finished{false};

    explicit FakeWatcher(std::atomic<int>& starts) : starts_ref(&starts) {}

    void run() override {
        starts_ref->fetch_add(1);
        while (!stop_requested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        finished.store(true, std::memory_order_release);
    }

    void stop() override {
        stop_requested.store(true, std::memory_order_release);
    }

private:
    std::atomic<int>* starts_ref;
};

// Produces FakeWatchers, tracks how many have been created. The
// factory returns shared_ptrs aliased into `created` so tests can
// inspect state after the provider has released its own references.
struct TestFactory {
    std::atomic<int>                            starts{0};
    std::atomic<int>                            creations{0};
    std::vector<std::shared_ptr<FakeWatcher>>   created;

    WatcherFactory callable() {
        return [this](const Allocation&, Registry&) -> std::shared_ptr<IWatcher> {
            auto sp = std::make_shared<FakeWatcher>(starts);
            created.push_back(sp);
            creations.fetch_add(1);
            return sp;
        };
    }
};

Allocation alloc(std::string c, std::string j, AllocationState s) {
    Allocation a; a.id = c + ":" + j; a.cluster = std::move(c);
    a.jobid = std::move(j); a.state = s;
    return a;
}

}  // namespace

TEST(WatcherHookProvider, EmptyRegistryMeansNoThreads) {
    Registry reg;
    TestFactory factory;
    ClusterWatcherHookProvider p(reg, factory.callable());
    ShellState state{};
    p.on_startup(state);
    EXPECT_EQ(p.watcher_count(), 0u);
    EXPECT_EQ(factory.creations.load(), 0);
    p.on_exit(state);
}

TEST(WatcherHookProvider, OneThreadPerRunningAllocation) {
    Registry reg;
    reg.add_allocation(alloc("c1", "1", AllocationState::Running));
    reg.add_allocation(alloc("c1", "2", AllocationState::Ended));
    reg.add_allocation(alloc("c2", "3", AllocationState::Running));
    reg.add_allocation(alloc("c2", "4", AllocationState::Pending));

    TestFactory factory;
    ClusterWatcherHookProvider p(reg, factory.callable());
    ShellState state{};
    p.on_startup(state);

    // Two Running allocations; Ended and Pending should be skipped.
    EXPECT_EQ(p.watcher_count(), 2u);
    EXPECT_EQ(factory.creations.load(), 2);

    // Give threads a moment to actually start (run() calls start_counter++).
    for (int i = 0; i < 100 && factory.starts.load() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(factory.starts.load(), 2);

    p.on_exit(state);

    // After on_exit: every watcher observed stop_requested and exited.
    for (const auto& w : factory.created) {
        EXPECT_TRUE(w->stop_requested.load());
        EXPECT_TRUE(w->finished.load());
    }
}

TEST(WatcherHookProvider, OnExitJoinsQuickly) {
    Registry reg;
    reg.add_allocation(alloc("c1", "1", AllocationState::Running));
    reg.add_allocation(alloc("c2", "2", AllocationState::Running));
    reg.add_allocation(alloc("c3", "3", AllocationState::Running));

    TestFactory factory;
    ClusterWatcherHookProvider p(reg, factory.callable());
    ShellState state{};
    p.on_startup(state);
    // Give threads time to start.
    for (int i = 0; i < 100 && factory.starts.load() < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto t0 = std::chrono::steady_clock::now();
    p.on_exit(state);
    const auto dt = std::chrono::steady_clock::now() - t0;

    // FakeWatchers poll every 2ms; join should complete well under 200ms.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count(),
              200);
}

TEST(WatcherHookProvider, HandlesRepeatedOnExitWithoutCrash) {
    Registry reg;
    reg.add_allocation(alloc("c1", "1", AllocationState::Running));
    TestFactory factory;
    ClusterWatcherHookProvider p(reg, factory.callable());
    ShellState state{};
    p.on_startup(state);
    p.on_exit(state);
    p.on_exit(state);   // second call must be a no-op.
    SUCCEED();
}

TEST(WatcherHookProvider, DestructorAlsoJoinsRunningThreads) {
    Registry reg;
    reg.add_allocation(alloc("c1", "1", AllocationState::Running));
    TestFactory factory;
    {
        ClusterWatcherHookProvider p(reg, factory.callable());
        ShellState state{};
        p.on_startup(state);
        // Skip explicit on_exit — the destructor must clean up.
    }
    // If we got here without SEGV or hang, watcher cleanup is safe.
    SUCCEED();
}

TEST(WatcherHookProvider, DefaultFactoryReturnsUsableWatcher) {
    auto factory = default_watcher_factory();
    Registry reg;
    Allocation a = alloc("c1", "1", AllocationState::Running);
    auto w = factory(a, reg);
    ASSERT_NE(w, nullptr);
    // We can call stop() before run() without UB.
    w->stop();
}

// ══════════════════════════════════════════════════════════════════════════════
// Regression: a watcher whose run() ignores stop() and sleeps past the
// join_backstop must NOT
//   (a) block on_exit forever,
//   (b) std::terminate when threads_ is cleared with a joinable thread,
//   (c) leave the joiner referencing destroyed stack state after detach,
//   (d) leave the detached thread dereferencing a destroyed IWatcher.
// ══════════════════════════════════════════════════════════════════════════════
namespace {

class HungWatcher : public IWatcher {
public:
    std::atomic<bool> started{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> finished{false};
    std::chrono::milliseconds run_for;

    explicit HungWatcher(std::chrono::milliseconds d) : run_for(d) {}

    void run() override {
        started.store(true, std::memory_order_release);
        // Ignore the stop flag intentionally — simulate a blocking
        // syscall that can't be cancelled by a flag poll (this is the
        // real-world ssh-tail case).
        std::this_thread::sleep_for(run_for);
        finished.store(true, std::memory_order_release);
    }

    void stop() override {
        stop_requested.store(true, std::memory_order_release);
    }
};

struct HungFactory {
    std::vector<std::shared_ptr<HungWatcher>> created;
    std::chrono::milliseconds run_for{std::chrono::milliseconds{300}};

    WatcherFactory callable() {
        return [this](const Allocation&, Registry&) -> std::shared_ptr<IWatcher> {
            auto sp = std::make_shared<HungWatcher>(run_for);
            created.push_back(sp);
            return sp;
        };
    }
};

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// make_ssh_tail_watcher_factory — returns nullptr (opt-out) when
// either resolver yields empty; otherwise returns a real watcher.
// ══════════════════════════════════════════════════════════════════════════════
namespace {

class DummyNotifier : public INotifier {
public:
    void desktop(const std::string&, const std::string&) override {}
    void bell() override {}
};

}  // namespace

TEST(SshTailWatcherFactory, EmptyHostOrDirYieldsNullOptOut) {
    DummyNotifier n;
    auto factory_empty_host = make_ssh_tail_watcher_factory(
        [](const Allocation&) { return std::string{}; },
        [](const Allocation&) { return std::string{"/tmp/evt"}; },
        n);
    Registry r;
    auto w1 = factory_empty_host(alloc("c1", "1", AllocationState::Running), r);
    EXPECT_EQ(w1, nullptr);

    auto factory_empty_dir = make_ssh_tail_watcher_factory(
        [](const Allocation&) { return std::string{"ssh-host"}; },
        [](const Allocation&) { return std::string{}; },
        n);
    auto w2 = factory_empty_dir(alloc("c1", "1", AllocationState::Running), r);
    EXPECT_EQ(w2, nullptr);
}

TEST(SshTailWatcherFactory, ReturnsNonNullWhenResolversProvideValues) {
    DummyNotifier n;
    auto factory = make_ssh_tail_watcher_factory(
        [](const Allocation& a) { return "ssh-" + a.cluster; },
        [](const Allocation& a) { return "/tmp/" + a.jobid + "/events"; },
        n);
    Registry r;
    auto w = factory(alloc("c1", "42", AllocationState::Running), r);
    ASSERT_NE(w, nullptr);
    // Immediately stop — the underlying ssh is not installed on the
    // test host. We don't exercise run(); we only assert that the
    // factory produced something we can cleanly tear down.
    w->stop();
}

TEST(WatcherHookProvider, HungWatcherRespectsBackstopThenDetaches) {
    Registry reg;
    reg.add_allocation(alloc("c1", "1", AllocationState::Running));
    reg.add_allocation(alloc("c2", "2", AllocationState::Running));

    HungFactory factory;
    factory.run_for = std::chrono::milliseconds{300};
    ClusterWatcherHookProvider p(reg, factory.callable());
    p.join_backstop = std::chrono::milliseconds{30};

    ShellState state{};
    p.on_startup(state);

    // Wait until both threads are in run() before asking them to stop,
    // otherwise we might fast-path through join() before they block.
    for (int i = 0; i < 500; ++i) {
        int started = 0;
        for (const auto& w : factory.created) {
            if (w->started.load(std::memory_order_acquire)) ++started;
        }
        if (started == static_cast<int>(factory.created.size())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto t0 = std::chrono::steady_clock::now();
    p.on_exit(state);
    const auto dt = std::chrono::steady_clock::now() - t0;

    // Returned near the backstop — proves we didn't block for run_for.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count(),
              250);

    // Give the detached thread time to actually finish its sleep so its
    // HungWatcher must still be alive when finished is written. If the
    // provider destroyed watchers on clear(), this store would UAF.
    std::this_thread::sleep_for(factory.run_for + std::chrono::milliseconds{200});
    for (const auto& w : factory.created) {
        EXPECT_TRUE(w->finished.load(std::memory_order_acquire));
    }
}
