#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/core.h"
#include "theme.h"
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>
#include <ctime>
#include <iostream>

using namespace std;

// ── Key management ────────────────────────────────────────────

string ai_get_key_path() {
    // Allow override for testing (avoids clobbering real key)
    const char *override = getenv("TASH_AI_KEY_PATH");
    if (override && override[0] != '\0') return string(override);

    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_ai_key";
}

string ai_load_key() {
    string path = ai_get_key_path();
    if (path.empty()) return "";

    ifstream file(path);
    if (!file.is_open()) return "";

    string key;
    getline(file, key);

    while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' '))
        key.pop_back();

    return key;
}

bool ai_save_key(const string &key) {
    string path = ai_get_key_path();
    if (path.empty()) return false;

    ofstream file(path, ios::trunc);
    if (!file.is_open()) return false;

    file << key << "\n";
    file.close();

    chmod(path.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

bool ai_run_setup_wizard() {
    write_stdout("\n");
    write_stdout(AI_LABEL "tash ai" CAT_RESET AI_SEPARATOR " ─ " CAT_RESET
                 "AI features require a free Google Gemini API key.\n\n");
    write_stdout("  How to get one (it's free):\n");
    write_stdout(AI_STEP_NUM "  1." CAT_RESET " Go to: " AI_CMD "https://aistudio.google.com/apikey" CAT_RESET "\n");
    write_stdout(AI_STEP_NUM "  2." CAT_RESET " Sign in with your Google account\n");
    write_stdout(AI_STEP_NUM "  3." CAT_RESET " Click \"Create API Key\"\n");
    write_stdout(AI_STEP_NUM "  4." CAT_RESET " Copy the key\n\n");
    write_stdout("  Paste your API key here: ");

    string key;
    if (!getline(cin, key) || key.empty()) {
        write_stdout("\n" AI_ERROR "  Setup cancelled." CAT_RESET "\n\n");
        return false;
    }

    // Strip bracketed paste escape sequences (\e[200~ and \e[201~)
    string bp_start = "\033[200~";
    string bp_end = "\033[201~";
    size_t pos;
    while ((pos = key.find(bp_start)) != string::npos)
        key.erase(pos, bp_start.size());
    while ((pos = key.find(bp_end)) != string::npos)
        key.erase(pos, bp_end.size());

    while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' '))
        key.pop_back();
    while (!key.empty() && key.front() == ' ')
        key.erase(key.begin());

    if (key.empty()) {
        write_stdout(AI_ERROR "  No key provided." CAT_RESET "\n\n");
        return false;
    }

    if (!ai_save_key(key)) {
        write_stdout(AI_ERROR "  Failed to save API key." CAT_RESET "\n\n");
        return false;
    }

    write_stdout("\n" AI_CMD "  API key saved." CAT_RESET
                 " Try: " AI_LABEL "@ai \"list all files modified today\"" CAT_RESET "\n\n");
    return true;
}

bool ai_validate_key(const string &key) {
    return !key.empty() && key.size() >= 10;
}

// ── Backend selection ─────────────────────────────────────────

static string trim_ws(string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    return s;
}

static string backend_file_path() {
    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_ai_backend";
}

static string ollama_model_file_path() {
    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_ollama_model";
}

static string read_first_line(const string &path) {
    if (path.empty()) return "";
    ifstream f(path);
    if (!f.is_open()) return "";
    string line;
    getline(f, line);
    return trim_ws(line);
}

static bool write_file(const string &path, const string &contents) {
    if (path.empty()) return false;
    ofstream f(path, ios::trunc);
    if (!f.is_open()) return false;
    f << contents << "\n";
    return true;
}

const char *ai_backend_name(AIBackend backend) {
    return backend == AI_BACKEND_OLLAMA ? "ollama" : "gemini";
}

AIBackend ai_get_backend() {
    const char *env = getenv("TASH_AI_BACKEND");
    string val;
    if (env && env[0] != '\0') {
        val = trim_ws(string(env));
    } else {
        val = read_first_line(backend_file_path());
    }
    if (val == "ollama") return AI_BACKEND_OLLAMA;
    return AI_BACKEND_GEMINI;
}

bool ai_set_backend(AIBackend backend) {
    return write_file(backend_file_path(), ai_backend_name(backend));
}

string ai_get_ollama_url() {
    const char *env = getenv("TASH_OLLAMA_URL");
    if (env && env[0] != '\0') return string(env);
    return "http://localhost:11434";
}

string ai_get_ollama_model() {
    const char *env = getenv("TASH_OLLAMA_MODEL");
    if (env && env[0] != '\0') return string(env);
    string stored = read_first_line(ollama_model_file_path());
    if (!stored.empty()) return stored;
    return "llama3.2";
}

bool ai_set_ollama_model(const string &model) {
    return write_file(ollama_model_file_path(), model);
}

// ── Usage tracking ────────────────────────────────────────────

string ai_get_usage_path() {
    const char *override = getenv("TASH_AI_USAGE_PATH");
    if (override && override[0] != '\0') return string(override);

    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_ai_usage";
}

// Usage file format: YYYY-MM-DD|count
// One line per day, only the current day matters

static string today_str() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return string(buf);
}

int ai_get_today_usage() {
    string path = ai_get_usage_path();
    if (path.empty()) return 0;

    ifstream file(path);
    if (!file.is_open()) return 0;

    string today = today_str();
    string line;
    while (getline(file, line)) {
        size_t sep = line.find('|');
        if (sep != string::npos && line.substr(0, sep) == today) {
            try {
                return stoi(line.substr(sep + 1));
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

void ai_increment_usage() {
    string path = ai_get_usage_path();
    if (path.empty()) return;

    string today = today_str();
    int count = ai_get_today_usage() + 1;

    // Rewrite with only today's entry (prunes old days)
    ofstream file(path, ios::trunc);
    if (file.is_open()) {
        file << today << "|" << count << "\n";
    }
}

#endif // TASH_AI_ENABLED
