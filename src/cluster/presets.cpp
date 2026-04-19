// Preset resolution. See include/tash/cluster/presets.h for the contract.

#include "tash/cluster/presets.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// $VAR / ${VAR} expansion using getenv. Matches config.cpp's semantics:
// unknown vars expand to empty; unterminated refs are left as literals.
std::string expand_env(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    const auto is_name_ch = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '$' && i + 1 < s.size()) {
            if (s[i + 1] == '{') {
                const auto end = s.find('}', i + 2);
                if (end == std::string_view::npos) { out += s[i]; ++i; continue; }
                const std::string name(s.substr(i + 2, end - i - 2));
                if (const char* v = std::getenv(name.c_str())) out += v;
                i = end + 1;
                continue;
            }
            std::size_t end = i + 1;
            while (end < s.size() && is_name_ch(s[end])) ++end;
            if (end > i + 1) {
                const std::string name(s.substr(i + 1, end - i - 1));
                if (const char* v = std::getenv(name.c_str())) out += v;
                i = end;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// Resolve the TASH_CLUSTER_STOP_HOOKS_DIR, preferring the runtime env var
// so packaged installs / tests can point somewhere else without rebuilding.
std::string stop_hooks_dir() {
    if (const char* env = std::getenv("TASH_CLUSTER_STOP_HOOKS_DIR")) {
        return env;
    }
#ifdef TASH_CLUSTER_STOP_HOOKS_DIR
    return TASH_CLUSTER_STOP_HOOKS_DIR;
#else
    return {};
#endif
}

// Trim ASCII whitespace from both ends.
std::string_view trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Strip one layer of enclosing matched quotes, if present.
std::string strip_quotes(std::string_view v) {
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') ||
                          (v.front() == '\'' && v.back() == '\''))) {
        return std::string(v.substr(1, v.size() - 2));
    }
    return std::string(v);
}

// Parse one line of an env file. Returns true if a pair was produced.
bool parse_env_line(std::string_view raw,
                     std::string& key_out,
                     std::string& val_out) {
    auto line = trim(raw);
    if (line.empty() || line.front() == '#') return false;

    // Strip optional "export " prefix.
    constexpr std::string_view kExport = "export ";
    if (line.size() > kExport.size() &&
        line.substr(0, kExport.size()) == kExport) {
        line = trim(line.substr(kExport.size()));
    }

    const auto eq = line.find('=');
    if (eq == std::string_view::npos) return false;

    auto k = trim(line.substr(0, eq));
    auto v = trim(line.substr(eq + 1));
    if (k.empty()) return false;

    key_out = std::string(k);
    val_out = strip_quotes(v);
    return true;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// source_env_file
// ══════════════════════════════════════════════════════════════════════════════

std::map<std::string, std::string> source_env_file(const std::filesystem::path& path) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    if (!f.is_open()) return out;

    std::string line;
    while (std::getline(f, line)) {
        std::string k, v;
        if (parse_env_line(line, k, v)) out[k] = v;
    }
    return out;
}

// ══════════════════════════════════════════════════════════════════════════════
// resolve_preset
// ══════════════════════════════════════════════════════════════════════════════

PresetResolveResult resolve_preset(const Preset& preset) {
    ResolvedPreset rp;
    rp.name    = preset.name;
    rp.command = expand_env(preset.command);

    // ── env_file ──────────────────────────────────────────
    if (preset.env_file && !preset.env_file->empty()) {
        const std::filesystem::path p(*preset.env_file);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || ec) {
            PresetResolveError err;
            err.message = "preset '" + preset.name + "': env_file does not exist: " + *preset.env_file;
            return err;
        }
        rp.env_vars = source_env_file(p);
    }

    // ── stop_hook ─────────────────────────────────────────
    if (preset.stop_hook && !preset.stop_hook->empty()) {
        const std::string& h = *preset.stop_hook;

        constexpr std::string_view kBuiltinPrefix = "builtin:";
        if (h.substr(0, kBuiltinPrefix.size()) == kBuiltinPrefix) {
            const std::string name = h.substr(kBuiltinPrefix.size());
            if (name == "claude") {
                const auto dir = stop_hooks_dir();
                if (dir.empty()) {
                    PresetResolveError err;
                    err.message = "preset '" + preset.name + "': builtin stop_hook requested but "
                                  "TASH_CLUSTER_STOP_HOOKS_DIR is unset";
                    return err;
                }
                rp.stop_hook_path = (std::filesystem::path(dir) / "claude-stop-hook.sh").string();
            } else {
                PresetResolveError err;
                err.message = "preset '" + preset.name + "': unknown builtin stop_hook: " + name;
                return err;
            }
        } else if (!h.empty() && h.front() == '/') {
            rp.stop_hook_path = h;
        } else {
            PresetResolveError err;
            err.message = "preset '" + preset.name + "': stop_hook must be \"builtin:<name>\" "
                          "or an absolute path (got: \"" + h + "\")";
            return err;
        }
    }

    return rp;
}

}  // namespace tash::cluster
