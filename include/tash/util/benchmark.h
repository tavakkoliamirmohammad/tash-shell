#ifndef TASH_UTIL_BENCHMARK_H
#define TASH_UTIL_BENCHMARK_H

#include <chrono>
#include <string>
#include <vector>

// ── Format helpers ────────────────────────────────────────────

// Format a duration in milliseconds for human-readable display.
//   < 1ms:     "0.3ms"
//   1-999ms:   "14.4ms"
//   >= 1000ms: "1.23s"
std::string format_duration_ms(double ms);

// Pad a string with trailing spaces to the given width.
// If the string is already >= width, it is returned unchanged.
std::string pad_right(const std::string &s, size_t width);

// ── Startup benchmark timer ───────────────────────────────────

class StartupBenchmark {
public:
    // Begin timing a named stage. The previous stage (if any) must
    // have been closed with end() before starting a new one.
    void start(const std::string &stage_name);

    // Finish the current stage and record its duration.
    void end();

    // Per-stage result returned by results().
    struct StageResult {
        std::string name;
        double duration_ms;
    };

    // Return completed stage results in order.
    std::vector<StageResult> results() const;

    // Sum of all completed stage durations (milliseconds).
    double total_ms() const;

    // Formatted multi-line report:
    //   Startup breakdown:
    //     Binary load:       2.1ms
    //     Config parse:      1.3ms
    //     ...
    //     ──────────────────────────
    //     Total:            14.4ms
    std::string report() const;

private:
    struct Stage {
        std::string name;
        std::chrono::high_resolution_clock::time_point start_time;
        double duration_ms;
        bool completed;
    };
    std::vector<Stage> stages_;
};

#endif // TASH_UTIL_BENCHMARK_H
