#include "shell.h"
#include <cmath>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>

using namespace std;

struct DirEntry {
    string path;
    int frequency;
    time_t last_access;
};

static string z_data_path() {
    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_z";
}

static vector<DirEntry> load_z_data() {
    vector<DirEntry> entries;
    string path = z_data_path();
    if (path.empty()) return entries;

    ifstream file(path);
    if (!file.is_open()) return entries;

    string line;
    while (getline(file, line)) {
        // Format: path|frequency|last_access
        size_t p1 = line.find('|');
        if (p1 == string::npos) continue;
        size_t p2 = line.find('|', p1 + 1);
        if (p2 == string::npos) continue;

        DirEntry e;
        e.path = line.substr(0, p1);
        try {
            e.frequency = stoi(line.substr(p1 + 1, p2 - p1 - 1));
            e.last_access = (time_t)stol(line.substr(p2 + 1));
        } catch (...) {
            continue;
        }
        entries.push_back(e);
    }
    return entries;
}

static void save_z_data(const vector<DirEntry> &entries) {
    string path = z_data_path();
    if (path.empty()) return;

    ofstream file(path, ios::trunc);
    if (!file.is_open()) return;

    for (const auto &e : entries) {
        file << e.path << "|" << e.frequency << "|" << e.last_access << "\n";
    }
}

static double frecency_score(const DirEntry &e) {
    time_t now = time(nullptr);
    double age_hours = difftime(now, e.last_access) / 3600.0;

    // Frecency: frequency weighted by recency
    double recency_weight;
    if (age_hours < 1) recency_weight = 4.0;
    else if (age_hours < 24) recency_weight = 2.0;
    else if (age_hours < 168) recency_weight = 1.0;  // 1 week
    else recency_weight = 0.5;

    return e.frequency * recency_weight;
}

void z_record_directory(const string &dir) {
    vector<DirEntry> entries = load_z_data();
    time_t now = time(nullptr);

    bool found = false;
    for (auto &e : entries) {
        if (e.path == dir) {
            e.frequency++;
            e.last_access = now;
            found = true;
            break;
        }
    }

    if (!found) {
        entries.push_back({dir, 1, now});
    }

    // Prune old/low-score entries if list gets too long
    if (entries.size() > 200) {
        sort(entries.begin(), entries.end(), [](const DirEntry &a, const DirEntry &b) {
            return frecency_score(a) > frecency_score(b);
        });
        entries.resize(150);
    }

    save_z_data(entries);
}

string z_find_directory(const string &query) {
    vector<DirEntry> entries = load_z_data();

    // Score each entry by frecency, filtered by query match
    string best;
    double best_score = -1;

    string lower_query = query;
    transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    for (const auto &e : entries) {
        // Case-insensitive substring match on the path
        string lower_path = e.path;
        transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

        if (lower_path.find(lower_query) == string::npos) continue;

        // Check directory still exists
        struct stat st;
        if (stat(e.path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        double score = frecency_score(e);
        if (score > best_score) {
            best_score = score;
            best = e.path;
        }
    }

    return best;
}
