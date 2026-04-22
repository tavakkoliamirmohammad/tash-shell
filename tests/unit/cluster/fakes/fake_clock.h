// FakeClock — deterministic clock + sleep for engine tests.
// now() returns a stored time point. sleep_for() advances it by the
// given duration (no real sleep). advance() lets a test jump forward
// explicitly.

#ifndef TASH_CLUSTER_FAKE_CLOCK_H
#define TASH_CLUSTER_FAKE_CLOCK_H

#include "tash/cluster/cluster_engine.h"

#include <chrono>

namespace tash::cluster::testing {

class FakeClock : public IClock {
public:
    std::chrono::steady_clock::time_point t_{};
    std::chrono::milliseconds              total_slept{0};

    std::chrono::steady_clock::time_point now() override { return t_; }

    void sleep_for(std::chrono::milliseconds d) override {
        total_slept += d;
        t_ += d;
    }

    void advance(std::chrono::milliseconds d) { t_ += d; }

    void reset() {
        t_           = {};
        total_slept  = std::chrono::milliseconds{0};
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_FAKE_CLOCK_H
