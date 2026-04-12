#include "shell.h"
#include <dirent.h>
#include <algorithm>

using namespace std;

static vector<string> path_commands;

static int levenshtein(const string &a, const string &b) {
    int m = a.size(), n = b.size();
    vector<vector<int>> dp(m + 1, vector<int>(n + 1));
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = min({dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost});
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

    // Also add builtins
    const auto &builtins = get_builtins();
    for (const auto &pair : builtins) {
        if (seen.insert(pair.first).second) {
            path_commands.push_back(pair.first);
        }
    }
}

string suggest_command(const string &cmd) {
    if (path_commands.empty()) build_command_cache();

    string best;
    int best_dist = 999;
    int max_dist = max(1, (int)cmd.size() / 3 + 1);  // allow ~1 typo per 3 chars

    for (const string &candidate : path_commands) {
        // Quick length filter
        int len_diff = (int)candidate.size() - (int)cmd.size();
        if (len_diff > max_dist || len_diff < -max_dist) continue;

        int dist = levenshtein(cmd, candidate);
        if (dist < best_dist && dist > 0 && dist <= max_dist) {
            best_dist = dist;
            best = candidate;
        }
    }
    return best;
}
