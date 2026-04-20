
#include "tash/ai.h"
#include "tash/core/signals.h"
#include "tash/util/config_resolver.h"
#include "tash/util/io.h"
#include "theme.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <algorithm>
#include <optional>

using namespace std;

// ── XDG config directory ─────────────────────────────────────

string ai_get_config_dir() {
    return tash::config::get_config_dir();
}

static bool ensure_config_dir() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return false;
    if (!tash::config::ensure_dir(dir)) return false;

    struct stat st{};
    if (lstat(dir.c_str(), &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            static bool warned = false;
            if (!warned) {
                tash::io::error("refusing to operate on " + dir
                                + " — it is a symbolic link; key files would be exposed");
                warned = true;
            }
            return false;  // Refuse to write keys through a symlink.
        }
        if (!S_ISDIR(st.st_mode)) {
            return false;  // Not a directory; ensure_dir should have caught this, but defensive.
        }
        mode_t current = st.st_mode & 0777;
        if (current != 0700) {
            if (chmod(dir.c_str(), 0700) == 0) {
                // Only log once per process to avoid noise on every key access.
                static bool logged = false;
                if (!logged) {
                    tash::io::info("tightened permissions on " + dir + " to 0700");
                    logged = true;
                }
            }
        }
    }
    return true;
}

// Atomically create/truncate `path` with owner-only perms and write `content`.
// Returns true on full write.
static bool write_secure_file(const string &path, const string &content) {
    if (path.empty()) return false;
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        tash::io::error("could not open " + path + ": " + strerror(errno));
        return false;
    }
    // If the file already existed with looser perms, tighten now.
    (void)fchmod(fd, 0600);
    size_t off = 0;
    while (off < content.size()) {
        ssize_t w = ::write(fd, content.data() + off, content.size() - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        off += static_cast<size_t>(w);
    }
    // Durability: fsync before close so a crash between `close` and the
    // filesystem journal flush does not leave a truncated/zero-length key.
    // fsync failure is logged but not fatal — data is already written.
    if (::fsync(fd) != 0) {
        tash::io::warning("fsync on " + path + ": " + strerror(errno));
    }
    bool ok = (::close(fd) == 0);
    return ok;
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

// Distinguishes "absent" from "present but unreadable". The older
// read_file_line collapsed both to "" which made `@ai status` display
// "not configured" even when the key file existed but had wrong perms
// (e.g. recovered from a tarball without preserving 0600).
static KeyLoadResult classify_file_read(const string &path) {
    KeyLoadResult out;
    out.status = KeyStatus::Absent;
    if (path.empty()) {
        out.status = KeyStatus::Absent;
        return out;
    }
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) {
        // ENOENT is "absent"; EACCES / other errors are "unreadable".
        if (errno == ENOENT) {
            out.status = KeyStatus::Absent;
        } else {
            out.status = KeyStatus::Unreadable;
            out.diagnostic = strerror(errno);
        }
        return out;
    }
    if (!S_ISREG(st.st_mode)) {
        out.status = KeyStatus::Unreadable;
        out.diagnostic = "not a regular file";
        return out;
    }
    ifstream file(path);
    if (!file.is_open()) {
        out.status = KeyStatus::Unreadable;
        out.diagnostic = strerror(errno);
        return out;
    }
    string line;
    if (!getline(file, line) && file.bad()) {
        out.status = KeyStatus::Unreadable;
        out.diagnostic = "read failed";
        return out;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    if (line.empty()) {
        out.status = KeyStatus::Empty;
        return out;
    }
    out.status = KeyStatus::Ok;
    out.value = std::move(line);
    return out;
}

// ── Helper: write single-line file ───────────────────────────

static bool write_file_line(const string &path, const string &content) {
    if (path.empty()) return false;
    if (!ensure_config_dir()) return false;
    return write_secure_file(path, content + "\n");
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

std::optional<std::string> ai_get_model_override() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return std::nullopt;
    std::string v = read_file_line(dir + "/ai_model");
    if (v.empty()) return std::nullopt;
    return v;
}

void ai_set_model_override(const string &model) {
    string dir = ai_get_config_dir();
    if (dir.empty()) return;
    write_file_line(dir + "/ai_model", model);
}

std::optional<std::string> ai_load_provider_key(const string &provider) {
    KeyLoadResult r = ai_load_provider_key_ex(provider);
    if (r.status != KeyStatus::Ok) return std::nullopt;
    return r.value;
}

KeyLoadResult ai_load_provider_key_ex(const string &provider) {
    KeyLoadResult out;
    if (provider != "gemini" && provider != "openai" && provider != "ollama") {
        out.status = KeyStatus::Absent;
        return out;
    }
    string dir = ai_get_config_dir();
    if (dir.empty()) {
        out.status = KeyStatus::Unreadable;
        out.diagnostic = "config dir unavailable";
        return out;
    }
    return classify_file_read(dir + "/" + provider + "_key");
}

bool ai_save_provider_key(const string &provider, const string &key) {
    if (provider != "gemini" && provider != "openai" && provider != "ollama") return false;
    string dir = ai_get_config_dir();
    if (dir.empty()) return false;
    string path = dir + "/" + provider + "_key";
    if (!ensure_config_dir()) return false;
    return write_secure_file(path, key + "\n");
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

// Meyers-singleton rate limiter shared by @ai and the error-recovery hook.
// 10 RPM matches the Gemini free tier; OpenAI is higher but using the same
// bucket prevents either path from starving the other. Hardcoded because
// the limit is keyed to the lowest-common-denominator provider — raising
// it without provider-awareness would just move the 429s.
AiRateLimiter& global_ai_rate_limiter() {
    static AiRateLimiter lim(10, 60);
    return lim;
}

// ── Privacy preference ───────────────────────────────────────

bool ai_get_send_stderr() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return true;  // conservative default: preserve old behavior
    // File present with "0" => off; anything else or absent => on.
    string val = read_file_line(dir + "/ai_send_stderr");
    if (val.empty()) return true;
    return !(val == "0" || val == "off" || val == "false" || val == "no");
}

void ai_set_send_stderr(bool on) {
    string dir = ai_get_config_dir();
    if (dir.empty()) return;
    write_file_line(dir + "/ai_send_stderr", on ? "1" : "0");
}

bool ai_privacy_banner_pending() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return false;      // can't persist => don't loop forever
    string marker = dir + "/ai_banner_shown";
    struct stat st{};
    return stat(marker.c_str(), &st) != 0;   // pending iff marker missing
}

void ai_privacy_banner_mark_shown() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return;
    ensure_config_dir();
    write_file_line(dir + "/ai_banner_shown", "1");
}

// ── Conversation persistence ─────────────────────────────────

string ai_get_conversation_path() {
    string dir = ai_get_config_dir();
    if (dir.empty()) return "";
    return dir + "/ai_conversation.json";
}

std::vector<ConversationTurn> ai_load_conversation() {
    std::vector<ConversationTurn> out;
    string path = ai_get_conversation_path();
    if (path.empty()) return out;
    ifstream f(path);
    if (!f.is_open()) return out;     // absent or unreadable — start fresh
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    if (text.empty()) return out;
    try {
        auto j = nlohmann::json::parse(text);
        if (!j.is_array()) return out;
        for (const auto &t : j) {
            if (!t.is_object()) continue;
            if (!t.contains("role") || !t.contains("text")) continue;
            if (!t["role"].is_string() || !t["text"].is_string()) continue;
            ConversationTurn turn;
            turn.role = t["role"].get<std::string>();
            turn.text = t["text"].get<std::string>();
            out.push_back(std::move(turn));
        }
    } catch (const std::exception &) {
        // Corrupt file — start over rather than leaking the parse
        // error into every @ai call. The next save will overwrite it.
        return {};
    }
    return out;
}

void ai_save_conversation(const std::vector<ConversationTurn> &turns) {
    string path = ai_get_conversation_path();
    if (path.empty()) return;
    if (!ensure_config_dir()) return;
    nlohmann::json j = nlohmann::json::array();
    for (const auto &t : turns) {
        j.push_back({{"role", t.role}, {"text", t.text}});
    }
    // Conversation content can include paths, hostnames, failed-command
    // stderr — same sensitivity class as key files. 0600.
    write_secure_file(path, j.dump() + "\n");
}

void ai_clear_conversation_file() {
    string path = ai_get_conversation_path();
    if (path.empty()) return;
    ::unlink(path.c_str());
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
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return;
    (void)::fchmod(fd, 0600);

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

