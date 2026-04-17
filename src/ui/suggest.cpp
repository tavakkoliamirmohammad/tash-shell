#include "tash/core.h"
#include "tash/ui.h"
#include <dirent.h>
#include <algorithm>
#include <optional>

using namespace std;

static vector<string> path_commands;

// Damerau-Levenshtein distance: like Levenshtein but also counts
// adjacent character transpositions (ab→ba) as a single operation.
// This correctly handles typos like "gti" → "git" as distance 1.
static int damerau_levenshtein(const string &a, const string &b) {
    int m = a.size(), n = b.size();
    vector<vector<int>> dp(m + 1, vector<int>(n + 1));
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = min({dp[i - 1][j] + 1,       // deletion
                            dp[i][j - 1] + 1,        // insertion
                            dp[i - 1][j - 1] + cost}); // substitution
            // Transposition
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                dp[i][j] = min(dp[i][j], dp[i - 2][j - 2] + 1);
            }
        }
    }
    return dp[m][n];
}

void build_command_cache() {
    path_commands.clear();
    const char *path_env = getenv("PATH");
    if (!path_env) return;

    string path_str = path_env;
    size_t start = 0;
    unordered_set<string> seen;

    while (start < path_str.size()) {
        size_t end = path_str.find(':', start);
        if (end == string::npos) end = path_str.size();
        string dir_path = path_str.substr(start, end - start);
        start = end + 1;

        DIR *dir = opendir(dir_path.c_str());
        if (!dir) continue;
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (seen.insert(name).second) {
                path_commands.push_back(name);
            }
        }
        closedir(dir);
    }

    const auto &builtins = get_builtins();
    for (const auto &pair : builtins) {
        if (seen.insert(pair.first).second) {
            path_commands.push_back(pair.first);
        }
    }
}

const vector<string>& get_path_commands() {
    if (path_commands.empty()) build_command_cache();
    return path_commands;
}

bool command_exists_on_path(const string &cmd) {
    if (path_commands.empty()) build_command_cache();
    for (const string &c : path_commands) {
        if (c == cmd) return true;
    }
    return false;
}

std::optional<std::string> suggest_command(const string &cmd) {
    if (path_commands.empty()) build_command_cache();

    string best;
    int best_dist = 999;
    int max_dist = max(1, (int)cmd.size() / 3 + 1);

    for (const string &candidate : path_commands) {
        int len_diff = (int)candidate.size() - (int)cmd.size();
        if (len_diff > max_dist || len_diff < -max_dist) continue;

        int dist = damerau_levenshtein(cmd, candidate);
        if (dist < best_dist && dist > 0 && dist <= max_dist) {
            best_dist = dist;
            best = candidate;
        }
    }
    if (best.empty()) return std::nullopt;
    return best;
}
