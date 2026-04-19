// Tests for ClusterWatcherHookProvider lifecycle.
//
// Strategy: inject a test WatcherFactory that returns FakeWatchers
// (tiny RAII-ish classes that record start/stop and block on an
// atomic flag). Assert the provider spawns exactly one thread per
// Running allocation and cleanly joins them all on on_exit.

#include <gtest/gtest.h>

#include "tash/plugins/cluster_watcher_hook_provider.h"
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

// Produces FakeWatchers, tracks how many have been created.
struct TestFactory {
    std::atomic<int>                            starts{0};
    std::atomic<int>                            creations{0};
    std::vector<std::shared_ptr<FakeWatcher>>   created;   // keep alive + inspect

    WatcherFactory callable() {
        return [this](const Allocation&, Registry&) -> std::unique_ptr<IWatcher> {
            auto raw = std::make_unique<FakeWatcher>(starts);
            // Stash a non-owning shared_ptr so tests can inspect state.
            auto sp  = std::shared_ptr<FakeWatcher>(raw.get(), [](FakeWatcher*){});
            created.push_back(sp);
            creations.fetch_add(1);
            return raw;
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
