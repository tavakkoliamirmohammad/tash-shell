#include "tash/util/benchmark.h"

#include <iomanip>
#include <numeric>
#include <sstream>

// ── Format helpers ────────────────────────────────────────────

std::string format_duration_ms(double ms) {
    std::ostringstream oss;
    if (ms >= 1000.0) {
        oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << "s";
    } else {
        // Use default stream formatting (no fixed/setprecision) so that
        // values like 14.4 render as "14.4ms" rather than "14.400ms".
        oss << ms << "ms";
    }
    return oss.str();
}

std::string pad_right(const std::string &s, size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

// ── StartupBenchmark ──────────────────────────────────────────

void StartupBenchmark::start(const std::string &stage_name) {
    Stage stage;
    stage.name = stage_name;
    stage.start_time = std::chrono::high_resolution_clock::now();
    stage.duration_ms = 0.0;
    stage.completed = false;
    stages_.push_back(stage);
}

void StartupBenchmark::end() {
    if (stages_.empty()) return;
    Stage &current = stages_.back();
    if (current.completed) return;

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = now - current.start_time;
    current.duration_ms = elapsed.count();
    current.completed = true;
}

std::vector<StartupBenchmark::StageResult> StartupBenchmark::results() const {
    std::vector<StageResult> out;
    for (const Stage &s : stages_) {
        if (s.completed) {
            StageResult r;
            r.name = s.name;
            r.duration_ms = s.duration_ms;
            out.push_back(r);
        }
    }
    return out;
}

double StartupBenchmark::total_ms() const {
    double sum = 0.0;
    for (const Stage &s : stages_) {
        if (s.completed) {
            sum += s.duration_ms;
        }
    }
    return sum;
}

std::string StartupBenchmark::report() const {
    std::vector<StageResult> res = results();
    if (res.empty()) {
        return "Startup breakdown:\n  (no stages recorded)\n";
    }

    // Find the longest stage name for alignment.
    size_t max_name = 0;
    for (const StageResult &r : res) {
        if (r.name.size() > max_name) max_name = r.name.size();
    }
    // "Name:" plus padding — add 1 for the colon.
    size_t label_width = max_name + 1;

    std::ostringstream oss;
    oss << "Startup breakdown:\n";

    for (const StageResult &r : res) {
        std::string label = r.name + ":";
        oss << "  " << pad_right(label, label_width) << "  "
            << format_duration_ms(r.duration_ms) << "\n";
    }

    // Separator line: 2 indent + label_width + 2 gap + some extra.
    size_t sep_len = label_width + 16;
    std::string sep;
    // Use plain dashes for portability in C++14 / narrow strings.
    for (size_t i = 0; i < sep_len; ++i) {
        // U+2500 BOX DRAWINGS LIGHT HORIZONTAL = 0xE2 0x94 0x80 in UTF-8.
        sep += "\xe2\x94\x80";
    }
    oss << "  " << sep << "\n";

    oss << "  " << pad_right("Total:", label_width) << "  "
        << format_duration_ms(total_ms()) << "\n";

    return oss.str();
}
