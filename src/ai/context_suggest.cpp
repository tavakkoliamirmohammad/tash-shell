
#include "tash/ai.h"
#include <fstream>
#include <cstdlib>

using namespace std;

// Extract command key (first word, or first two for compound commands)
static string command_key(const string &line) {
    if (line.empty()) return "";

    size_t start = 0;
    while (start < line.size() && line[start] == ' ') start++;
    size_t end = line.find(' ', start);
    if (end == string::npos) return line.substr(start);

    string first = line.substr(start, end - start);

    // For compound commands, use first two words as key
    if (first == "git" || first == "docker" || first == "npm" ||
        first == "cargo" || first == "kubectl" || first == "brew" ||
        first == "apt" || first == "pip" || first == "yarn") {
        size_t start2 = end;
        while (start2 < line.size() && line[start2] == ' ') start2++;
        size_t end2 = line.find(' ', start2);
        if (end2 == string::npos) end2 = line.size();
        if (start2 < line.size()) {
            return first + " " + line.substr(start2, end2 - start2);
        }
    }

    return first;
}

void build_transition_map(const string &history_path, TransitionMap &tmap) {
    tmap.transitions.clear();

    ifstream file(history_path);
    if (!file.is_open()) return;

    string prev_key;
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;

        string key = command_key(line);
        if (key.empty()) continue;

        if (!prev_key.empty() && prev_key != key) {
            tmap.transitions[prev_key][line]++;
        }
        prev_key = key;
    }
}

string context_suggest(const string &last_command, const TransitionMap &tmap) {
    string key = command_key(last_command);
    if (key.empty()) return "";

    auto it = tmap.transitions.find(key);
    if (it == tmap.transitions.end()) return "";

    string best;
    int best_count = 0;

    // Default threshold: 3 (configurable via TASH_SUGGEST_THRESHOLD)
    int threshold = 3;
    const char *env_thresh = getenv("TASH_SUGGEST_THRESHOLD");
    if (env_thresh) {
        try { threshold = stoi(string(env_thresh)); } catch (...) {}
        if (threshold < 1) threshold = 1;
    }

    for (const auto &pair : it->second) {
        if (pair.second >= threshold && pair.second > best_count) {
            best_count = pair.second;
            best = pair.first;
        }
    }

    return best;
}

TransitionMap& get_transition_map() {
    static TransitionMap instance;
    return instance;
}

