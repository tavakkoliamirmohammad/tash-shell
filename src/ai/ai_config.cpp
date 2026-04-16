#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/core.h"
#include "theme.h"
#include <fstream>
#include <sys/stat.h>
#include <sys/file.h>
#include <termios.h>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <algorithm>

using namespace std;

// ── XDG config directory ─────────────────────────────────────

string ai_get_config_dir() {
    const char *override_dir = getenv("TASH_AI_CONFIG_DIR");
    if (override_dir && override_dir[0] != '\0') return string(override_dir);

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') return string(xdg) + "/tash";

    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.config/tash";
}

static bool ensure_config_dir() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return false;
    // Create parent ~/.config if needed
    string parent = dir.substr(0, dir.rfind('/'));
    if (mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) return false;
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) return false;
    return true;
}

// ── Helper: read single-line file ────────────────────────────

static string read_file_line(const string &path) {
    if (path.empty()) return "";
    ifstream file(path);
    if (!file.is_open()) return "";
    string line;
    getline(file, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

// ── Helper: write single-line file ───────────────────────────

static bool write_file_line(const string &path, const string &content) {
    if (path.empty()) return false;
    ensure_config_dir();
    ofstream file(path, ios::trunc);
    if (!file.is_open()) return false;
    file << content << "\n";
    bool ok = file.good();
    file.close();
    return ok;
}

// ── Key management ────────────────────────────────────────────

string ai_get_key_path() {
    // Allow override for testing (avoids clobbering real key)
    const char *override_path = getenv("TASH_AI_KEY_PATH");
    if (override_path && override_path[0] != '\0') return string(override_path);

    string dir = ai_get_config_dir();
    if (dir.empty()) return "";
    return dir + "/ai_key";
}

string ai_load_key() {
    return read_file_line(ai_get_key_path());
}

bool ai_save_key(const string &key) {
    string path = ai_get_key_path();
    if (path.empty()) return false;

    ensure_config_dir();
    ofstream file(path, ios::trunc);
    if (!file.is_open()) return false;

    file << key << "\n";
    file.close();

    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        write_stderr("tash: warning: could not set permissions on " + path + "\n");
    }
    return true;
}

// ── Provider config functions ────────────────────────────────

string ai_get_provider() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return "gemini";
    string val = read_file_line(dir + "/ai_provider");
    return val.empty() ? "gemini" : val;
}

void ai_set_provider(const string &provider) {
    string dir = ai_get_config_dir();
    if (dir.empty()) return;
    write_file_line(dir + "/ai_provider", provider);
}

string ai_get_model_override() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return "";
    return read_file_line(dir + "/ai_model");
}

void ai_set_model_override(const string &model) {
    string dir = ai_get_config_dir();
    if (dir.empty()) return;
    write_file_line(dir + "/ai_model", model);
}

string ai_load_provider_key(const string &provider) {
    if (provider != "gemini" && provider != "openai" && provider != "ollama") return "";
    string dir = ai_get_config_dir();
    if (dir.empty()) return "";
    return read_file_line(dir + "/" + provider + "_key");
}

bool ai_save_provider_key(const string &provider, const string &key) {
    if (provider != "gemini" && provider != "openai" && provider != "ollama") return false;
    string dir = ai_get_config_dir();
    if (dir.empty()) return false;

    string path = dir + "/" + provider + "_key";
    ensure_config_dir();
    ofstream file(path, ios::trunc);
    if (!file.is_open()) return false;

    file << key << "\n";
    file.close();

    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        write_stderr("tash: warning: could not set permissions on " + path + "\n");
    }
    return true;
}

string ai_get_ollama_url() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return "http://localhost:11434";
    string val = read_file_line(dir + "/ollama_url");
    return val.empty() ? "http://localhost:11434" : val;
}

void ai_set_ollama_url(const string &url) {
    string dir = ai_get_config_dir();
    if (dir.empty()) return;
    write_file_line(dir + "/ollama_url", url);
}

// ── Setup wizard ─────────────────────────────────────────────

bool ai_run_setup_wizard() {
    string provider = ai_get_provider();

    write_stdout("\n");
    write_stdout(AI_LABEL + "tash ai" CAT_RESET + AI_SEPARATOR + " ─ " CAT_RESET
                 "AI features configuration\n\n");

    if (provider == "gemini") {
        write_stdout("  Current provider: " + AI_CMD + "Gemini" CAT_RESET "\n\n");
        write_stdout("  How to get a free API key:\n");
        write_stdout(AI_STEP_NUM + "  1." CAT_RESET " Go to: " + AI_CMD + "https://aistudio.google.com/apikey" CAT_RESET "\n");
        write_stdout(AI_STEP_NUM + "  2." CAT_RESET " Sign in with your Google account\n");
        write_stdout(AI_STEP_NUM + "  3." CAT_RESET " Click \"Create API Key\"\n");
        write_stdout(AI_STEP_NUM + "  4." CAT_RESET " Copy the key\n\n");
    } else if (provider == "openai") {
        write_stdout("  Current provider: " + AI_CMD + "OpenAI" CAT_RESET "\n\n");
        write_stdout("  How to get an API key:\n");
        write_stdout(AI_STEP_NUM + "  1." CAT_RESET " Go to: " + AI_CMD + "https://platform.openai.com/api-keys" CAT_RESET "\n");
        write_stdout(AI_STEP_NUM + "  2." CAT_RESET " Sign in to your OpenAI account\n");
        write_stdout(AI_STEP_NUM + "  3." CAT_RESET " Create a new API key\n");
        write_stdout(AI_STEP_NUM + "  4." CAT_RESET " Copy the key\n\n");
    } else if (provider == "ollama") {
        write_stdout("  Current provider: " + AI_CMD + "Ollama (local)" CAT_RESET "\n\n");
        write_stdout("  Ollama runs locally — no API key needed.\n");
        write_stdout("  Make sure Ollama is running: " + AI_CMD + "ollama serve" CAT_RESET "\n\n");
        write_stdout(AI_CMD + "  Configuration saved." CAT_RESET "\n\n");
        return true;
    } else {
        write_stdout("  Current provider: " + AI_CMD + provider + CAT_RESET "\n\n");
    }

    write_stdout("  Paste your API key here: ");

    // Disable echo so bracketed paste sequences don't show
    struct termios old_term, new_term;
    bool term_modified = false;
    if (isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &old_term);
        new_term = old_term;
        new_term.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        term_modified = true;
    }

    string key;
    bool got_input = (bool)getline(cin, key);

    // Restore echo
    if (term_modified) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    }

    if (!got_input || key.empty()) {
        write_stdout("\n" + AI_ERROR + "  Setup cancelled." CAT_RESET "\n\n");
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
        write_stdout("\n" + AI_ERROR + "  No key provided." CAT_RESET "\n\n");
        return false;
    }

    // Show masked confirmation
    string masked;
    if (key.size() > 8) {
        masked = key.substr(0, 4) + "..." + key.substr(key.size() - 4);
    } else {
        masked = "****";
    }
    write_stdout(CAT_DIM + masked + CAT_RESET "\n");

    // Save to provider-specific key file
    if (!ai_save_provider_key(provider, key)) {
        write_stdout(AI_ERROR + "  Failed to save API key." CAT_RESET "\n\n");
        return false;
    }

    write_stdout("\n" + AI_CMD + "  API key saved." CAT_RESET
                 " Try: " + AI_LABEL + "@ai \"list all files modified today\"" CAT_RESET "\n\n");
    return true;
}

bool ai_validate_key(const string &key) {
    return !key.empty() && key.size() >= 39;
}

// ── Rate limiter ─────────────────────────────────────────────

AiRateLimiter::AiRateLimiter(int max_requests, int window_seconds)
    : max_requests_(max_requests), window_seconds_(window_seconds) {}

bool AiRateLimiter::allow() {
    time_t now = time(NULL);
    time_t cutoff = now - window_seconds_;

    // Prune timestamps older than the window
    vector<time_t> valid;
    for (size_t i = 0; i < timestamps_.size(); i++) {
        if (timestamps_[i] > cutoff) {
            valid.push_back(timestamps_[i]);
        }
    }
    timestamps_ = valid;

    if (static_cast<int>(timestamps_.size()) >= max_requests_) {
        return false;
    }

    timestamps_.push_back(now);
    return true;
}

// ── Usage tracking ────────────────────────────────────────────

string ai_get_usage_path() {
    const char *override_path = getenv("TASH_AI_USAGE_PATH");
    if (override_path && override_path[0] != '\0') return string(override_path);

    string dir = ai_get_config_dir();
    if (dir.empty()) return "";
    return dir + "/ai_usage";
}

// Usage file format: YYYY-MM-DD|count
// One line per day, only the current day matters

static string today_str() {
    time_t now = time(NULL);
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
            } catch (const std::exception &) {
                return 0;
            }
        }
    }
    return 0;
}

void ai_increment_usage() {
    string path = ai_get_usage_path();
    if (path.empty()) return;

    ensure_config_dir();

    // Use file locking to prevent races between concurrent sessions
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) return;

    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return;
    }

    // Read current count under lock
    string today = today_str();
    int count = 0;
    {
        ifstream in(path);
        string line;
        while (getline(in, line)) {
            size_t sep = line.find('|');
            if (sep != string::npos && line.substr(0, sep) == today) {
                try { count = stoi(line.substr(sep + 1)); } catch (const std::exception &) {}
            }
        }
    }
    count++;

    // Rewrite with only today's entry (prunes old days)
    if (ftruncate(fd, 0) == 0) {
        lseek(fd, 0, SEEK_SET);
        string entry = today + "|" + to_string(count) + "\n";
        if (write(fd, entry.c_str(), entry.size())) {}
    }

    flock(fd, LOCK_UN);
    close(fd);
}

#endif // TASH_AI_ENABLED
