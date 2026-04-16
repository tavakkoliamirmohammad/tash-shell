#include <gtest/gtest.h>
#include "tash/util/benchmark.h"

#include <algorithm>
#include <cmath>
#include <thread>

// ═══════════════════════════════════════════════════════════════
// StartupBenchmark tests
// ═══════════════════════════════════════════════════════════════

TEST(StartupBenchmarkTest, StartAndEnd) {
    StartupBenchmark bench;
    bench.start("test");
    bench.end();

    auto res = bench.results();
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].name, "test");
}

TEST(StartupBenchmarkTest, MultipleStages) {
    StartupBenchmark bench;
    bench.start("stage1");
    bench.end();
    bench.start("stage2");
    bench.end();
    bench.start("stage3");
    bench.end();

    auto res = bench.results();
    ASSERT_EQ(res.size(), 3u);
    EXPECT_EQ(res[0].name, "stage1");
    EXPECT_EQ(res[1].name, "stage2");
    EXPECT_EQ(res[2].name, "stage3");
}

TEST(StartupBenchmarkTest, DurationPositive) {
    StartupBenchmark bench;
    bench.start("work");
    // Small busy-wait so the clock advances measurably.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bench.end();

    auto res = bench.results();
    ASSERT_EQ(res.size(), 1u);
    EXPECT_GE(res[0].duration_ms, 0.0);
}

TEST(StartupBenchmarkTest, TotalSumOfStages) {
    StartupBenchmark bench;
    bench.start("a");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bench.end();
    bench.start("b");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bench.end();

    auto res = bench.results();
    double sum = 0.0;
    for (const auto &r : res) {
        sum += r.duration_ms;
    }
    EXPECT_DOUBLE_EQ(bench.total_ms(), sum);
}

TEST(StartupBenchmarkTest, ReportContainsStageNames) {
    StartupBenchmark bench;
    bench.start("Binary load");
    bench.end();
    bench.start("Config parse");
    bench.end();

    std::string report = bench.report();
    EXPECT_NE(report.find("Binary load"), std::string::npos);
    EXPECT_NE(report.find("Config parse"), std::string::npos);
}

TEST(StartupBenchmarkTest, ReportContainsTotal) {
    StartupBenchmark bench;
    bench.start("init");
    bench.end();

    std::string report = bench.report();
    EXPECT_NE(report.find("Total:"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Format helper tests
// ═══════════════════════════════════════════════════════════════

TEST(FormatDurationTest, FormatDurationSubMs) {
    EXPECT_EQ(format_duration_ms(0.3), "0.3ms");
}

TEST(FormatDurationTest, FormatDurationMs) {
    EXPECT_EQ(format_duration_ms(14.4), "14.4ms");
}

TEST(FormatDurationTest, FormatDurationSeconds) {
    EXPECT_EQ(format_duration_ms(1234.5), "1.23s");
}

TEST(PadRightTest, PadRight) {
    std::string result = pad_right("hi", 10);
    EXPECT_EQ(result, "hi        ");
    EXPECT_EQ(result.size(), 10u);
}

// ═══════════════════════════════════════════════════════════════
// Edge case
// ═══════════════════════════════════════════════════════════════

TEST(StartupBenchmarkTest, EmptyBenchmark) {
    StartupBenchmark bench;
    EXPECT_TRUE(bench.results().empty());
    EXPECT_DOUBLE_EQ(bench.total_ms(), 0.0);
}
