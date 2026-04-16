#include "tash/core/session.h"
#include "tash/shell.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────

// Escape a value so that it can be stored on a single key=value line.
// We only need to handle newlines and backslashes.
static std::string escape_value(const std::string &v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

// Unescape a value that was stored with escape_value().
static std::string unescape_value(const std::string &v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char next = v[i + 1];
            if (next == '\\') {
                out += '\\';
                ++i;
            } else if (next == 'n') {
                out += '\n';
                ++i;
            } else if (next == 'r') {
                out += '\r';
                ++i;
            } else {
                out += v[i];
            }
        } else {
            out += v[i];
        }
    }
    return out;
}

// Create a directory (and parents) recursively.  Returns true on success
// or if the directory already exists.
static bool mkdir_p(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Find the parent directory.
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos != 0) {
        std::string parent = path.substr(0, pos);
        if (!mkdir_p(parent)) {
            return false;
        }
    }

    return mkdir(path.c_str(), 0755) == 0;
}

// Return the position of the first unescaped '=' in a line,
// or std::string::npos if not found.
static size_t find_separator(const std::string &line) {
    return line.find('=');
}

// ── Serialization ─────────────────────────────────────────────

bool save_session(const std::string &path, const SessionInfo &info) {
    // Ensure the parent directory exists.
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string parent = path.substr(0, slash);
        if (!mkdir_p(parent)) {
            return false;
        }
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return false;
    }

    ofs << "name=" << escape_value(info.name) << "\n";
    ofs << "cwd=" << escape_value(info.working_directory) << "\n";
    ofs << "created=" << info.created_at << "\n";
    ofs << "last_active=" << info.last_active << "\n";
    ofs << "socket=" << escape_value(info.socket_path) << "\n";

    // Aliases – sorted for deterministic output.
    {
        std::vector<std::string> keys;
        keys.reserve(info.aliases.size());
        for (auto it = info.aliases.begin(); it != info.aliases.end(); ++it) {
            keys.push_back(it->first);
        }
        std::sort(keys.begin(), keys.end());
        for (size_t i = 0; i < keys.size(); ++i) {
            const std::string &k = keys[i];
            ofs << "alias:" << escape_value(k) << "="
                << escape_value(info.aliases.at(k)) << "\n";
        }
    }

    // Environment variables – sorted for deterministic output.
    {
        std::vector<std::string> keys;
        keys.reserve(info.env_vars.size());
        for (auto it = info.env_vars.begin(); it != info.env_vars.end(); ++it) {
            keys.push_back(it->first);
        }
        std::sort(keys.begin(), keys.end());
        for (size_t i = 0; i < keys.size(); ++i) {
            const std::string &k = keys[i];
            ofs << "env:" << escape_value(k) << "="
                << escape_value(info.env_vars.at(k)) << "\n";
        }
    }

    ofs.flush();
    return ofs.good();
}

SessionInfo load_session(const std::string &path) {
    SessionInfo info;

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return info; // empty / default
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }

        size_t eq = find_separator(line);
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string raw_value = line.substr(eq + 1);
        std::string value = unescape_value(raw_value);

        if (key == "name") {
            info.name = value;
        } else if (key == "cwd") {
            info.working_directory = value;
        } else if (key == "created") {
            info.created_at = std::strtoll(raw_value.c_str(), nullptr, 10);
        } else if (key == "last_active") {
            info.last_active = std::strtoll(raw_value.c_str(), nullptr, 10);
        } else if (key == "socket") {
            info.socket_path = value;
        } else if (key.size() > 6 && key.substr(0, 6) == "alias:") {
            std::string alias_name = unescape_value(key.substr(6));
            info.aliases[alias_name] = value;
        } else if (key.size() > 4 && key.substr(0, 4) == "env:") {
            std::string env_name = unescape_value(key.substr(4));
            info.env_vars[env_name] = value;
        }
    }

    return info;
}

// ── Session directory management ──────────────────────────────

std::string get_sessions_dir() {
    const char *home = std::getenv("HOME");
    if (!home) {
        return "";
    }
    std::string base = std::string(home) + "/.tash/sessions";
    mkdir_p(base);
    return base + "/";
}

std::string get_sessions_dir(const std::string &base_dir) {
    std::string dir = base_dir;
    // Ensure trailing slash.
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    mkdir_p(dir.substr(0, dir.size() - 1)); // strip trailing slash for mkdir
    return dir;
}

std::vector<SessionInfo> list_sessions() {
    return list_sessions(get_sessions_dir());
}

std::vector<SessionInfo> list_sessions(const std::string &sessions_dir) {
    std::vector<SessionInfo> result;

    if (sessions_dir.empty()) {
        return result;
    }

    DIR *dp = opendir(sessions_dir.c_str());
    if (!dp) {
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string fname = entry->d_name;
        // Look for *.json files.
        if (fname.size() > 5 &&
            fname.substr(fname.size() - 5) == ".json") {
            std::string full_path = sessions_dir;
            if (!full_path.empty() && full_path.back() != '/') {
                full_path += '/';
            }
            full_path += fname;
            SessionInfo info = load_session(full_path);
            if (!info.name.empty()) {
                result.push_back(info);
            }
        }
    }

    closedir(dp);
    return result;
}

bool session_exists(const std::string &name) {
    return session_exists(name, get_sessions_dir());
}

bool session_exists(const std::string &name,
                    const std::string &sessions_dir) {
    if (sessions_dir.empty() || name.empty()) {
        return false;
    }
    std::string path = sessions_dir;
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    path += name + ".json";
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool delete_session(const std::string &name) {
    return delete_session(name, get_sessions_dir());
}

bool delete_session(const std::string &name,
                    const std::string &sessions_dir) {
    if (sessions_dir.empty() || name.empty()) {
        return false;
    }
    std::string path = sessions_dir;
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    path += name + ".json";
    return std::remove(path.c_str()) == 0;
}

// ── State capture / restore ───────────────────────────────────

// Well-known environment variable names that are interesting to capture.
static const char *const CAPTURED_ENV_VARS[] = {
    "EDITOR",
    "VISUAL",
    "PAGER",
    "LANG",
    "LC_ALL",
    "TERM",
    "GOPATH",
    "VIRTUAL_ENV",
    "PYTHONPATH",
    "NODE_PATH",
    "JAVA_HOME",
    nullptr
};

SessionInfo capture_current_state(const std::string &name,
                                  const ShellState &state) {
    SessionInfo info;
    info.name = name;

    // Working directory.
    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        info.working_directory = cwd_buf;
    }

    // Timestamps.
    info.created_at = static_cast<int64_t>(std::time(nullptr));
    info.last_active = info.created_at;

    // Aliases from ShellState.
    info.aliases = state.aliases;

    // Capture selected environment variables.
    for (const char *const *p = CAPTURED_ENV_VARS; *p; ++p) {
        const char *val = std::getenv(*p);
        if (val) {
            info.env_vars[*p] = val;
        }
    }

    return info;
}

void restore_session(const SessionInfo &info, ShellState &state) {
    // Restore working directory.
    if (!info.working_directory.empty()) {
        if (chdir(info.working_directory.c_str()) != 0) {
            // Silently ignore if the directory no longer exists.
        }
    }

    // Restore aliases – merge into ShellState (session aliases override).
    for (auto it = info.aliases.begin(); it != info.aliases.end(); ++it) {
        state.aliases[it->first] = it->second;
    }

    // Restore environment variables.
    for (auto it = info.env_vars.begin(); it != info.env_vars.end(); ++it) {
        setenv(it->first.c_str(), it->second.c_str(), 1);
    }
}
