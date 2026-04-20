// SLURM argv builders + stdout parsers. See
// include/tash/cluster/slurm_parse.h for the contract + output formats.

#include "tash/cluster/slurm_parse.h"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace tash::cluster::slurm_parse {

// ══════════════════════════════════════════════════════════════════════════════
// Small string helpers (file-local)
// ══════════════════════════════════════════════════════════════════════════════

namespace {

std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.emplace_back(s.substr(start));
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

std::string strip_trailing_asterisks(std::string s) {
    while (!s.empty() && s.back() == '*') s.pop_back();
    return s;
}

bool is_null_sentinel(const std::string& s) {
    return s == "(null)" || s == "N/A" || s == "n/a";
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Parsers
// ══════════════════════════════════════════════════════════════════════════════

std::vector<JobState> parse_squeue(std::string_view output) {
    std::vector<JobState> out;
    std::istringstream ss{std::string(output)};
    std::string line;
    while (std::getline(ss, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        const auto parts = split(trimmed, '|');
        if (parts.size() < 4) continue;                    // malformed row
        JobState js;
        js.jobid     = parts[0];
        js.state     = parts[1];
        js.node      = is_null_sentinel(parts[2]) ? "" : parts[2];
        js.time_left = parts[3];
        out.push_back(std::move(js));
    }
    return out;
}

std::vector<PartitionState> parse_sinfo(std::string_view output) {
    std::vector<PartitionState> out;
    std::istringstream ss{std::string(output)};
    std::string line;
    while (std::getline(ss, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        const auto parts = split(trimmed, '|');
        if (parts.size() < 3) continue;                    // malformed row

        PartitionState ps;
        ps.partition = parts[0];
        ps.state     = strip_trailing_asterisks(parts[1]);

        int n = 0;
        try { n = std::stoi(parts[2]); } catch (...) { n = 0; }

        if (ps.state == "idle") {
            ps.idle_nodes = n;
            const std::string gres = parts.size() >= 4 ? parts[3] : "";
            if (!gres.empty() && !is_null_sentinel(gres)) {
                for (int i = 0; i < n; ++i) ps.idle_gres.push_back(gres);
            }
        } else {
            ps.idle_nodes = 0;                             // only idle counts
        }
        out.push_back(std::move(ps));
    }
    return out;
}

std::string parse_sbatch_jobid(std::string_view output) {
    // Trim, then strip anything after a ';' (--parsable cluster suffix).
    std::string s{trim(output)};
    if (auto sc = s.find(';'); sc != std::string::npos) s = s.substr(0, sc);
    // Strip trailing newline
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();

    // Banner form: "Submitted batch job <N>"
    constexpr std::string_view kBanner = "Submitted batch job ";
    if (auto pos = s.find(kBanner); pos != std::string::npos) {
        pos += kBanner.size();
        std::string id;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
            id += s[pos++];
        }
        return id;
    }

    // --parsable form: bare digits (possibly with whitespace around).
    const auto t = trim(s);
    if (!t.empty()) {
        bool all_digits = true;
        for (char c : t) if (!std::isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; }
        if (all_digits) return std::string(t);
    }

    return {};
}

// ══════════════════════════════════════════════════════════════════════════════
// argv builders
// ══════════════════════════════════════════════════════════════════════════════

// Single-quote a string so the remote shell unquotes it back to the
// original bytes. Needed for fields we pass through ssh-to-remote-shell
// that may contain spaces (most commonly --wrap=<script body>).
static std::string shq(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::vector<std::string> build_sbatch_argv(const SubmitSpec& s) {
    std::vector<std::string> a = {"sbatch", "--parsable"};
    if (!s.account.empty())   a.push_back("--account="        + s.account);
    if (!s.partition.empty()) a.push_back("--partition="      + s.partition);
    if (!s.qos.empty())       a.push_back("--qos="            + s.qos);
    if (!s.gres.empty())      a.push_back("--gres="           + s.gres);
    if (!s.time.empty())      a.push_back("--time="           + s.time);
    if (s.cpus > 0)           a.push_back("--cpus-per-task="  + std::to_string(s.cpus));
    if (!s.mem.empty())       a.push_back("--mem="            + s.mem);
    if (!s.job_name.empty())  a.push_back("--job-name="       + s.job_name);
    // --wrap=<body> body often contains spaces ("sleep infinity").
    // ssh's "join with spaces, re-parse on remote" workflow requires
    // the body be shell-quoted or it becomes separate positional args
    // and sbatch refuses with "Script arguments not permitted".
    if (!s.wrap.empty())      a.push_back("--wrap="           + shq(s.wrap));
    return a;
}

std::vector<std::string> build_squeue_argv() {
    // --me filters to current user (slurm >= 20.11). -h suppresses header.
    // %i|%t|%N|%L is the stable format parse_squeue() expects.
    return {"squeue", "-h", "--me", "-o", "%i|%t|%N|%L"};
}

std::vector<std::string> build_sinfo_argv(const std::string& partition) {
    return {"sinfo", "-h", "-p", partition, "-o", "%P|%t|%D|%G"};
}

std::vector<std::string> build_scancel_argv(const std::string& jobid) {
    return {"scancel", jobid};
}

}  // namespace tash::cluster::slurm_parse
