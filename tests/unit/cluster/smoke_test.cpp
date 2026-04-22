// Smoke test for the cluster unit test target.
//
// This file exists to prove the test target is wired up correctly
// before any real ClusterEngine logic is written. Real tests for
// Config, Registry, ClusterEngine, seams, etc. land in M1.

#include <gtest/gtest.h>

TEST(ClusterSmoke, BuildsAndRuns) {
    SUCCEED();
}
