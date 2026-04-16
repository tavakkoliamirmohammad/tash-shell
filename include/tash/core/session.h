#ifndef TASH_CORE_SESSION_H
#define TASH_CORE_SESSION_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct ShellState;

// ── Session state ─────────────────────────────────────────────

struct SessionInfo {
    std::string name;
    std::string working_directory;
    std::unordered_map<std::string, std::string> env_vars;   // exported vars
    std::unordered_map<std::string, std::string> aliases;
    int64_t created_at;    // unix timestamp
    int64_t last_active;   // unix timestamp
    std::string socket_path;

    SessionInfo() : created_at(0), last_active(0) {}
};

// ── Session serialization ─────────────────────────────────────

// Write SessionInfo to a key=value file at the given path.
// Format:
//   name=mywork
//   cwd=/Users/amir/project
//   created=1713283200
//   last_active=1713290400
//   socket=
//   alias:gst=git status
//   env:MY_VAR=value
bool save_session(const std::string &path, const SessionInfo &info);

// Parse a key=value file back into SessionInfo.
// Returns a default-constructed (empty) SessionInfo on failure.
SessionInfo load_session(const std::string &path);

// ── Session directory management ──────────────────────────────

// Returns ~/.tash/sessions/, creating the directory if needed.
std::string get_sessions_dir();

// Overload that uses a custom base directory (for testing).
std::string get_sessions_dir(const std::string &base_dir);

// Scan the sessions directory for *.json files and load each.
std::vector<SessionInfo> list_sessions();

// Overload that scans a custom sessions directory (for testing).
std::vector<SessionInfo> list_sessions(const std::string &sessions_dir);

// Check whether ~/.tash/sessions/<name>.json exists.
bool session_exists(const std::string &name);

// Overload that checks in a custom sessions directory (for testing).
bool session_exists(const std::string &name,
                    const std::string &sessions_dir);

// Remove the session file. Returns true on success.
bool delete_session(const std::string &name);

// Overload that deletes from a custom sessions directory (for testing).
bool delete_session(const std::string &name,
                    const std::string &sessions_dir);

// ── Session state capture / restore ───────────────────────────

// Capture cwd, aliases, selected env vars, and timestamps from the
// current process and the given ShellState.
SessionInfo capture_current_state(const std::string &name,
                                  const ShellState &state);

// Restore a previously-captured session: chdir to saved cwd, restore
// aliases, and set environment variables.
void restore_session(const SessionInfo &info, ShellState &state);

#endif // TASH_CORE_SESSION_H
