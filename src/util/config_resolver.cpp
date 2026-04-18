#include "tash/util/config_resolver.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace tash::config {
namespace {

std::string env_or_empty(const char *name) {
    const char *v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

std::string home() {
    std::string h = env_or_empty("HOME");
    return h.empty() ? std::string("/tmp") : h;  // last-resort fallback
}

} // namespace

std::string get_config_dir() {
    std::string t = env_or_empty("TASH_CONFIG_HOME");
    if (!t.empty()) return t;
    std::string xdg = env_or_empty("XDG_CONFIG_HOME");
    if (!xdg.empty()) return xdg + "/tash";
    return home() + "/.config/tash";
}

std::string get_data_dir() {
    std::string t = env_or_empty("TASH_DATA_HOME");
    if (!t.empty()) return t;
    std::string xdg = env_or_empty("XDG_DATA_HOME");
    if (!xdg.empty()) return xdg + "/tash";
    return home() + "/.tash";
}

std::string get_tashrc_path()          { return home() + "/.tashrc"; }
std::string get_history_file_path()    { return home() + "/.tash_history"; }
std::string get_frecency_path()        { return home() + "/.tash_z"; }

std::string get_theme_toml_path()      { return get_config_dir() + "/theme.toml"; }
std::string get_theme_name_path()      { return get_config_dir() + "/theme.name"; }
std::string get_user_themes_dir()      { return get_config_dir() + "/themes"; }
std::string get_fig_completions_dir()  { return get_config_dir() + "/completions/fig"; }

std::string get_sessions_dir()         { return get_data_dir() + "/sessions"; }
std::string get_history_db_path()      { return get_data_dir() + "/history.db"; }

std::string get_starship_config_path() { return home() + "/.config/starship.toml"; }

bool ensure_dir(const std::string &path) {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    bool created_or_exists = !ec || std::filesystem::is_directory(path, ec);
    if (created_or_exists) {
        // Tighten to owner-only. The shell's data and config dirs hold
        // history, sessions, and api keys; they must not be readable by
        // other users on a shared box. chmod is best-effort -- tmpfs on
        // some CI runners silently ignores it and that's fine.
        (void)::chmod(path.c_str(), 0700);
    }
    return created_or_exists;
}

} // namespace tash::config
