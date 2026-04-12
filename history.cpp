#include "shell.h"
#include <sys/stat.h>

using namespace std;

static string history_file_path() {
    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_history";
}

void load_persistent_history() {
    string path = history_file_path();
    if (path.empty()) return;

    ifstream file(path);
    if (!file.is_open()) return;

    string line;
    while (getline(file, line)) {
        if (!line.empty()) {
            add_history(line.c_str());
        }
    }
}

void save_history_line(const string &line) {
    string path = history_file_path();
    if (path.empty()) return;

    ofstream file(path, ios::app);
    if (file.is_open()) {
        file << line << "\n";
    }
}

bool should_record_history(const string &line) {
    if (line.empty()) return false;
    if (line[0] == ' ') return false;

    if (history_length > 0) {
        HIST_ENTRY *last = history_get(history_base + history_length - 1);
        if (last && string(last->line) == line) {
            return false;
        }
    }

    return true;
}

static string search_prefix;

void reset_prefix_search() {
    search_prefix.clear();
}

// Prefix history search is only available with GNU readline (Linux).
// macOS libedit's readline compat layer lacks rl_bind_keyseq,
// rl_delete_text, and other APIs needed for this feature.
// On macOS, standard up/down history navigation and Ctrl-R search
// remain available through libedit's defaults.

#ifndef __APPLE__

static void replace_line_text(const char *text) {
    rl_delete_text(0, rl_end);
    rl_point = 0;
    rl_insert_text(text);
}

int history_search_prefix_backward(int count, int key) {
    (void)count; (void)key;
    string current(rl_line_buffer, rl_end);
    if (search_prefix.empty() || current != search_prefix) {
        if (rl_end > 0) {
            search_prefix = current;
        }
    }

    if (search_prefix.empty()) {
        rl_get_previous_history(1, 0);
        return 0;
    }

    int pos = where_history();
    while (pos > 0) {
        pos--;
        HIST_ENTRY *entry = history_get(history_base + pos);
        if (entry && string(entry->line).compare(0, search_prefix.size(), search_prefix) == 0) {
            history_set_pos(pos);
            replace_line_text(entry->line);
            rl_redisplay();
            return 0;
        }
    }
    return 0;
}

int history_search_prefix_forward(int count, int key) {
    (void)count; (void)key;
    if (search_prefix.empty()) {
        rl_get_next_history(1, 0);
        return 0;
    }

    int pos = where_history();
    int max_pos = history_length;
    while (pos < max_pos - 1) {
        pos++;
        HIST_ENTRY *entry = history_get(history_base + pos);
        if (entry && string(entry->line).compare(0, search_prefix.size(), search_prefix) == 0) {
            history_set_pos(pos);
            replace_line_text(entry->line);
            rl_redisplay();
            return 0;
        }
    }

    if (pos >= max_pos - 1) {
        history_set_pos(max_pos);
        replace_line_text(search_prefix.c_str());
        rl_redisplay();
    }
    return 0;
}

void setup_prefix_history_search() {
    rl_bind_keyseq("\033[A", history_search_prefix_backward);
    rl_bind_keyseq("\033[B", history_search_prefix_forward);
    rl_bind_keyseq("\033OA", history_search_prefix_backward);
    rl_bind_keyseq("\033OB", history_search_prefix_forward);
}

#else

// macOS stub: libedit handles Up/Down natively
void setup_prefix_history_search() {
    // No-op on macOS — libedit provides basic history navigation
}

#endif
