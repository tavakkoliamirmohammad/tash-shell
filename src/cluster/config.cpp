// Config TOML loader + validator. See include/tash/cluster/config.h for
// the contract. Implementation uses toml++ (fetched via cmake/cluster.cmake).

#include "tash/cluster/config.h"

#include <toml++/toml.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// ConfigError::format — builds "tash: cluster: <path>:<L>:<C>: <msg>".
// ══════════════════════════════════════════════════════════════════════════════

std::string ConfigError::format() const {
    std::ostringstream os;
    os << "tash: cluster: " << path;
    if (line > 0) {
        os << ':' << line;
        if (column > 0) os << ':' << column;
    }
    os << ": " << message;
    return os.str();
}

// ══════════════════════════════════════════════════════════════════════════════
// Helpers — kept in an anonymous namespace; none escape the translation unit.
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// Expand $VAR and ${VAR} using getenv. Unknown or unterminated references
// are left as-is. Keeps things simple and predictable — no recursion, no
// default values, no fallbacks.
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

// Source region of a toml++ node. If node is null, returns {0, 0}.
// toml++ reports 1-based line/column.
std::pair<int, int> region_of(const toml::node* n) {
    if (!n) return {0, 0};
    const auto& r = n->source();
    return {static_cast<int>(r.begin.line), static_cast<int>(r.begin.column)};
}

// Pretty helper: build a "missing required field" error at the containing
// table's region.
ConfigError missing_field_error(const std::string& path, const toml::node& where,
                                 const std::string& field) {
    ConfigError e;
    e.path    = path;
    auto [l, c] = region_of(&where);
    e.line    = l;
    e.column  = c;
    e.message = "missing required field: " + field;
    return e;
}

// Fetch a required string field. Returns false with err populated if absent.
bool require_string(const toml::table& tbl, const char* field,
                     const std::string& path, std::string& out, ConfigError& err) {
    if (auto v = tbl[field].value<std::string>()) { out = *v; return true; }
    err = missing_field_error(path, tbl, field);
    return false;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// ConfigLoader::load — read a file + dispatch to load_from_string.
// ══════════════════════════════════════════════════════════════════════════════

ConfigLoadResult ConfigLoader::load(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        ConfigError err;
        err.path    = path.string();
        err.message = "cannot open config file";
        return err;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    return load_from_string(buf.str(), path.string());
}

// ══════════════════════════════════════════════════════════════════════════════
// ConfigLoader::load_from_string — parse + validate.
// ══════════════════════════════════════════════════════════════════════════════

ConfigLoadResult ConfigLoader::load_from_string(const std::string& source,
                                                  const std::string& path) {
    toml::table tbl;
    try {
        tbl = toml::parse(source, path);
    } catch (const toml::parse_error& e) {
        ConfigError err;
        err.path    = path;
        err.line    = static_cast<int>(e.source().begin.line);
        err.column  = static_cast<int>(e.source().begin.column);
        err.message = std::string(e.description());
        return err;
    }

    Config cfg;

    // ── [defaults] ────────────────────────────────────────────
    if (auto* dft = tbl["defaults"].as_table()) {
        if (auto v = (*dft)["workspace_base"].value<std::string>())
            cfg.defaults.workspace_base = expand_env(*v);
        if (auto v = (*dft)["default_preset"].value<std::string>())
            cfg.defaults.default_preset = *v;
        if (auto v = (*dft)["control_persist"].value<std::string>())
            cfg.defaults.control_persist = *v;
        if (auto v = (*dft)["notify_silence_sec"].value<std::int64_t>())
            cfg.defaults.notify_silence_sec = static_cast<int>(*v);
    }

    // ── [[clusters]] ──────────────────────────────────────────
    if (auto* arr = tbl["clusters"].as_array()) {
        for (auto& elt : *arr) {
            const auto* cltbl = elt.as_table();
            if (!cltbl) continue;
            Cluster c;
            ConfigError err;
            if (!require_string(*cltbl, "name",     path, c.name,     err)) return err;
            if (!require_string(*cltbl, "ssh_host", path, c.ssh_host, err)) return err;
            if (auto v = (*cltbl)["description"].value<std::string>())
                c.description = *v;
            cfg.clusters.push_back(std::move(c));
        }
    }

    // ── [[resources]] ─────────────────────────────────────────
    if (auto* arr = tbl["resources"].as_array()) {
        for (auto& elt : *arr) {
            const auto* rtbl = elt.as_table();
            if (!rtbl) continue;
            Resource r;
            ConfigError err;
            if (!require_string(*rtbl, "name", path, r.name, err)) return err;

            if (auto kind = (*rtbl)["kind"].value<std::string>()) {
                if (*kind == "gpu")      r.kind = ResourceKind::Gpu;
                else if (*kind == "cpu") r.kind = ResourceKind::Cpu;
                else {
                    ConfigError e;
                    e.path = path;
                    auto [l, c] = region_of((*rtbl)["kind"].node());
                    e.line = l; e.column = c;
                    e.message = "resource kind must be \"gpu\" or \"cpu\" (got \"" + *kind + "\")";
                    return e;
                }
            }

            if (auto v = (*rtbl)["description"].value<std::string>())     r.description  = *v;
            if (auto v = (*rtbl)["default_time"].value<std::string>())    r.default_time = *v;
            if (auto v = (*rtbl)["default_cpus"].value<std::int64_t>())   r.default_cpus = static_cast<int>(*v);
            if (auto v = (*rtbl)["default_mem"].value<std::string>())     r.default_mem  = *v;

            if (const auto* routes = (*rtbl)["routes"].as_array()) {
                for (auto& rnode : *routes) {
                    const auto* rttbl = rnode.as_table();
                    if (!rttbl) continue;
                    Route route;
                    if (auto v = (*rttbl)["cluster"].value<std::string>())   route.cluster   = *v;
                    if (auto v = (*rttbl)["account"].value<std::string>())   route.account   = *v;
                    if (auto v = (*rttbl)["partition"].value<std::string>()) route.partition = *v;
                    if (auto v = (*rttbl)["qos"].value<std::string>())       route.qos       = *v;
                    if (auto v = (*rttbl)["gres"].value<std::string>())      route.gres      = *v;
                    r.routes.push_back(std::move(route));
                }
            }

            cfg.resources.push_back(std::move(r));
        }
    }

    // ── [[presets]] ───────────────────────────────────────────
    if (auto* arr = tbl["presets"].as_array()) {
        for (auto& elt : *arr) {
            const auto* ptbl = elt.as_table();
            if (!ptbl) continue;
            Preset p;
            ConfigError err;
            if (!require_string(*ptbl, "name",    path, p.name,    err)) return err;
            if (!require_string(*ptbl, "command", path, p.command, err)) return err;

            if (auto v = (*ptbl)["env_file"].value<std::string>())   p.env_file  = expand_env(*v);
            if (auto v = (*ptbl)["stop_hook"].value<std::string>())  p.stop_hook = *v;

            cfg.presets.push_back(std::move(p));
        }
    }

    // ── Cross-validation: every route.cluster must reference a declared cluster.
    for (const auto& r : cfg.resources) {
        for (const auto& route : r.routes) {
            bool found = false;
            for (const auto& c : cfg.clusters) {
                if (c.name == route.cluster) { found = true; break; }
            }
            if (!found) {
                ConfigError e;
                e.path    = path;
                e.line    = 0;
                e.column  = 0;
                e.message = "resource '" + r.name + "' route references unknown cluster: '"
                            + route.cluster + "'";
                return e;
            }
        }
    }

    return cfg;
}

// ══════════════════════════════════════════════════════════════════════════════
// Name-based lookups.
// ══════════════════════════════════════════════════════════════════════════════

const Cluster* find_cluster(const Config& c, std::string_view name) {
    for (const auto& x : c.clusters) if (x.name == name) return &x;
    return nullptr;
}
const Resource* find_resource(const Config& c, std::string_view name) {
    for (const auto& x : c.resources) if (x.name == name) return &x;
    return nullptr;
}
const Preset* find_preset(const Config& c, std::string_view name) {
    for (const auto& x : c.presets) if (x.name == name) return &x;
    return nullptr;
}

}  // namespace tash::cluster
