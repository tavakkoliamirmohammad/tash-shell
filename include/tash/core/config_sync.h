#ifndef TASH_CORE_CONFIG_SYNC_H
#define TASH_CORE_CONFIG_SYNC_H

#include <string>
#include <vector>

namespace tash::config_sync {

// ── Command result from git operations ────────────────────────

struct CmdResult {
    std::string output;
    int exit_code;
};

// ── Helper: run a git command in the given directory ──────────
//
// The legacy overload takes a shell-style arg string and splits it on
// whitespace for argv reconstruction. Prefer run_git_argv for anything
// that interpolates user-supplied data -- it bypasses the split entirely
// and feeds argv straight into the safe-exec path. The legacy overload
// is retained for callers/tests that pass a static literal.

CmdResult run_git_command(const std::string &config_dir,
                          const std::string &git_args);

CmdResult run_git_argv(const std::string &config_dir,
                       const std::vector<std::string> &git_args);

// ── Core API ─────────────────────────────────────────────────

/// Returns ~/.tash/, creates the directory if it does not exist.
std::string get_tash_config_dir();

/// Initialize a git repo in config_dir.
/// Creates .gitignore with sensible defaults.
/// Returns true on success.
bool sync_init(const std::string &config_dir);

/// Set (or update) the remote origin URL for the config repo.
/// Returns true on success.
bool sync_set_remote(const std::string &config_dir,
                     const std::string &remote_url);

/// Stage all changes, commit with a timestamp message, and push.
/// Returns true on success.
bool sync_push(const std::string &config_dir);

/// Pull latest changes from origin master.
/// Returns true on success.
bool sync_pull(const std::string &config_dir);

/// Returns combined output of `git diff HEAD` and `git status --short`.
std::string sync_diff(const std::string &config_dir);

/// Check whether config_dir has been initialized (i.e. .git/ exists).
bool sync_is_initialized(const std::string &config_dir);

} // namespace tash::config_sync

#endif // TASH_CORE_CONFIG_SYNC_H
