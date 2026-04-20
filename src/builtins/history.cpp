// History builtin.
//
// Prior to the SQLite-source-of-truth PR, `history` was a 30-line
// plain-text dumper that read replxx's `.tash_history`. The builtin now
// queries the primary IHistoryProvider (SQLite in the default build)
// with support for the filters the README has always promised:
//
//   history [-n N|--limit N]       last N entries (default 100)
//   history --here                 filter to the current working directory
//   history --failed               only commands that exited non-zero
//   history --search PATTERN       LIKE-style substring search on command
//   history stats                  aggregate view (totals + top commands/dirs)
//
// Falls back to the plain-text file when no history provider is
// registered (build-without-SQLite or user disabled the plugin) so the
// command stays useful in either configuration.

#include "tash/builtins.h"
#include "tash/core/signals.h"
#include "tash/history.h"
#include "tash/plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

namespace {

struct HistoryArgs {
    bool ok = true;
    string parse_error;
    bool want_stats = false;
    bool cwd_only = false;
    bool failed_only = false;
    int limit = 100;
    string search;
};

HistoryArgs parse_args(const vector<string> &argv) {
    HistoryArgs a;
    for (size_t i = 1; i < argv.size(); ++i) {
        const string &t = argv[i];
        if (t == "stats") {
            a.want_stats = true;
        } else if (t == "--here") {
            a.cwd_only = true;
        } else if (t == "--failed") {
            a.failed_only = true;
        } else if (t == "-n" || t == "--limit") {
            if (i + 1 >= argv.size()) {
                a.ok = false;
                a.parse_error = t + " needs a value";
                return a;
            }
            try { a.limit = stoi(argv[++i]); }
            catch (...) {
                a.ok = false; a.parse_error = "invalid --limit value: " + argv[i];
                return a;
            }
            if (a.limit <= 0) { a.limit = 100; }
        } else if (t.rfind("--limit=", 0) == 0) {
            try { a.limit = stoi(t.substr(8)); }
            catch (...) {
                a.ok = false; a.parse_error = "invalid --limit value: " + t.substr(8);
                return a;
            }
            if (a.limit <= 0) a.limit = 100;
        } else if (t == "--search") {
            if (i + 1 >= argv.size()) {
                a.ok = false;
                a.parse_error = "--search needs a pattern";
                return a;
            }
            a.search = argv[++i];
        } else if (t.rfind("--search=", 0) == 0) {
            a.search = t.substr(9);
        } else if (t == "-h" || t == "--help") {
            a.ok = false;   // fall through to usage print
            return a;
        } else if (!t.empty() && t[0] == '-') {
            a.ok = false;
            a.parse_error = "unknown flag: " + t;
            return a;
        } else {
            // Bare positional — treated as a search pattern for ergonomic
            // use (`history git push` = `history --search 'git push'`).
            a.search = (a.search.empty() ? t : a.search + " " + t);
        }
    }
    return a;
}

void print_usage(bool to_stderr = false) {
    string msg =
        "usage: history [options] [pattern]\n"
        "  -n, --limit N       show only the last N entries (default 100)\n"
        "  --here              restrict to commands run in the current directory\n"
        "  --failed            only show commands that exited non-zero\n"
        "  --search PATTERN    substring match on the command text\n"
        "  stats               print aggregate stats (top commands, success rate)\n"
        "  -h, --help          show this message\n";
    if (to_stderr) write_stderr(msg); else write_stdout(msg);
}

string ts_to_iso(int64_t ts) {
    if (ts == 0) return "-";
    time_t t = static_cast<time_t>(ts);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

// Rendering helper — bash-style `N  command` with a right-aligned number
// column that grows with the total count.
void render_entries(const vector<HistoryEntry> &entries,
                     bool show_meta) {
    if (entries.empty()) {
        write_stdout("(no history entries matched)\n");
        return;
    }
    // Oldest-first display: more natural to scroll through.
    vector<HistoryEntry> ordered(entries.rbegin(), entries.rend());
    int width = static_cast<int>(std::to_string(ordered.size()).size());
    for (size_t i = 0; i < ordered.size(); ++i) {
        const auto &e = ordered[i];
        ostringstream line;
        line << "  ";
        line.width(width);
        line << (i + 1) << "  ";
        if (show_meta) {
            line << ts_to_iso(e.timestamp) << "  "
                 << "[" << e.exit_code << "]  ";
        }
        line << e.command << "\n";
        write_stdout(line.str());
    }
}

int render_stats(const HistoryStats &s) {
    if (s.total_commands == 0) {
        write_stdout("(no history recorded yet)\n");
        return 0;
    }
    ostringstream out;
    out << "history stats\n\n"
        << "  total commands      " << s.total_commands << "\n"
        << "  unique commands     " << s.unique_commands << "\n"
        << "  failed commands     " << s.failed_commands << "\n"
        << "  success rate        ";
    out.precision(1);
    out << std::fixed << s.success_rate_pct << "%\n";
    out << "  earliest entry      " << ts_to_iso(s.earliest_timestamp) << "\n"
        << "  latest entry        " << ts_to_iso(s.latest_timestamp) << "\n";
    if (!s.top_commands.empty()) {
        out << "\n  top commands:\n";
        for (const auto &kv : s.top_commands) {
            out << "    " << kv.second << "\t" << kv.first << "\n";
        }
    }
    if (!s.top_directories.empty()) {
        out << "\n  top directories:\n";
        for (const auto &kv : s.top_directories) {
            out << "    " << kv.second << "\t" << kv.first << "\n";
        }
    }
    write_stdout(out.str());
    return 0;
}

// Fallback for builds where no history provider is registered (e.g.,
// TASH_SQLITE_ENABLED is off). Keeps the builtin functional — we just
// lose --here/--failed/stats. Prints the replxx plain-text file.
int render_plain_text_fallback(const HistoryArgs &args) {
    if (args.want_stats || args.cwd_only || args.failed_only) {
        write_stderr("history: this build has no SQLite backend — "
                     "--here/--failed/stats are unavailable.\n");
        return 2;
    }
    string path = history_file_path();
    if (path.empty()) {
        write_stderr("history: no history file\n");
        return 1;
    }
    ifstream file(path);
    if (!file.is_open()) {
        write_stderr("history: cannot read history\n");
        return 1;
    }
    // Skip replxx's `### timestamp` lines; they're not user-visible.
    vector<string> lines;
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        if (line.size() >= 3 && line.substr(0, 3) == "###") continue;
        if (!args.search.empty() &&
            line.find(args.search) == string::npos) continue;
        lines.push_back(line);
    }
    int skip = std::max(0, static_cast<int>(lines.size()) - args.limit);
    int width = static_cast<int>(std::to_string(lines.size()).size());
    for (size_t i = skip; i < lines.size(); ++i) {
        ostringstream out;
        out << "  ";
        out.width(width);
        out << (i + 1) << "  " << lines[i] << "\n";
        write_stdout(out.str());
    }
    return 0;
}

} // namespace

// Prepend the in-flight command. SQLite records post-dispatch, so
// without this the `history` call would omit itself — surprising for
// anyone used to bash's `history` (where the listing includes the line
// the user just typed). ShellState.ai.last_command_text is published
// pre-dispatch in repl.cpp specifically to give us this view.
static void prepend_in_flight(std::vector<HistoryEntry> &entries,
                                const ShellState &state) {
    const std::string &cur = state.ai.last_command_text;
    if (cur.empty()) return;
    // Avoid duplicate: if the latest SQLite row already matches, skip.
    if (!entries.empty() && entries.front().command == cur) return;
    HistoryEntry e;
    e.command = cur;
    e.timestamp = static_cast<int64_t>(std::time(nullptr));
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) e.directory = cwd;
    entries.insert(entries.begin(), std::move(e));
}

int builtin_history(const vector<string> &argv, ShellState &state) {
    HistoryArgs args = parse_args(argv);
    if (!args.ok) {
        if (!args.parse_error.empty()) {
            write_stderr("history: " + args.parse_error + "\n");
            print_usage(true);
            return 2;
        }
        print_usage(false);
        return 0;
    }

    auto &reg = global_plugin_registry();
    if (reg.history_provider_count() == 0) {
        return render_plain_text_fallback(args);
    }

    if (args.want_stats) {
        return render_stats(reg.history_stats());
    }

    SearchFilter filter;
    filter.limit = args.limit;
    if (args.failed_only) {
        // SQL schema stores 0 for success, non-zero for failure. The
        // interface's `exit_code` field only supports exact-match, so
        // "failed" can't be a single-value filter here; we use a wide
        // search and post-filter. Acceptable at limit=100.
        SearchFilter wide;
        wide.limit = std::max(args.limit * 4, 200);
        if (args.cwd_only) {
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd))) wide.directory = cwd;
        }
        auto entries = reg.search_history(args.search, wide);
        std::vector<HistoryEntry> failed;
        for (const auto &e : entries) {
            if (e.exit_code != 0) failed.push_back(e);
            if (static_cast<int>(failed.size()) >= args.limit) break;
        }
        render_entries(failed, /*show_meta=*/true);
        return 0;
    }

    if (args.cwd_only) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) filter.directory = cwd;
    }

    auto entries = reg.search_history(args.search, filter);
    prepend_in_flight(entries, state);
    // Plain listing when no filters are active (just "history"): keep
    // the classic line-number + command look so users don't get a
    // suddenly-different UI. --here/--failed get the richer meta view.
    bool show_meta = args.cwd_only || args.failed_only || !args.search.empty();
    render_entries(entries, show_meta);
    return 0;
}
