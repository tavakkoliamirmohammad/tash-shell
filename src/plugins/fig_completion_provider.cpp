#ifdef TASH_AI_ENABLED

#include "tash/plugins/fig_completion_provider.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <dirent.h>

// ── Utility: expand ~ in path ────────────────────────────────

static std::string expand_home(const std::string &path) {
    if (!path.empty() && path[0] == '~') {
        const char *home = std::getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

// ── Default spec directory ───────────────────────────────────

static std::string default_spec_dir() {
    return "~/.tash/completions/fig/";
}

// ── Constructor (default dir) ────────────────────────────────

FigCompletionProvider::FigCompletionProvider()
    : spec_dir_(expand_home(default_spec_dir())) {
    // Build index of available spec files
    DIR *dp = opendir(spec_dir_.c_str());
    if (!dp) return;

    struct dirent *entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string filename = entry->d_name;

        // Must end with .json
        if (filename.size() < 6) continue;
        if (filename.compare(filename.size() - 5, 5, ".json") != 0)
            continue;

        std::string cmd = filename.substr(0, filename.size() - 5);
        spec_index_[cmd] = spec_dir_ + filename;
    }
    closedir(dp);
}

// ── Constructor (explicit dir) ───────────────────────────────

FigCompletionProvider::FigCompletionProvider(const std::string &spec_dir)
    : spec_dir_(expand_home(spec_dir)) {
    // Ensure trailing slash
    if (!spec_dir_.empty() && spec_dir_.back() != '/') {
        spec_dir_ += '/';
    }

    DIR *dp = opendir(spec_dir_.c_str());
    if (!dp) return;

    struct dirent *entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string filename = entry->d_name;

        if (filename.size() < 6) continue;
        if (filename.compare(filename.size() - 5, 5, ".json") != 0)
            continue;

        std::string cmd = filename.substr(0, filename.size() - 5);
        spec_index_[cmd] = spec_dir_ + filename;
    }
    closedir(dp);
}

// ── ICompletionProvider interface ────────────────────────────

std::string FigCompletionProvider::name() const {
    return "fig";
}

int FigCompletionProvider::priority() const {
    return 20;
}

bool FigCompletionProvider::can_complete(const std::string &command) const {
    // Check if spec is already loaded or if a spec file exists
    {
        std::lock_guard<std::mutex> lock(spec_mutex_);
        if (specs_.find(command) != specs_.end()) {
            return true;
        }
    }
    return spec_index_.find(command) != spec_index_.end();
}

std::vector<Completion> FigCompletionProvider::complete(
    const std::string &command,
    const std::string &current_word,
    const std::vector<std::string> &args,
    const ShellState & /*state*/) const {

    // Lazy-load the spec
    if (!load_spec(command)) {
        return {};
    }

    std::lock_guard<std::mutex> lock(spec_mutex_);
    auto it = specs_.find(command);
    if (it == specs_.end()) {
        return {};
    }

    return traverse_and_complete(it->second, current_word, args);
}

// ── Introspection ────────────────────────────────────────────

size_t FigCompletionProvider::loaded_spec_count() const {
    std::lock_guard<std::mutex> lock(spec_mutex_);
    return specs_.size();
}

bool FigCompletionProvider::has_spec(const std::string &command) const {
    {
        std::lock_guard<std::mutex> lock(spec_mutex_);
        if (specs_.find(command) != specs_.end()) {
            return true;
        }
    }
    return spec_index_.find(command) != spec_index_.end();
}

// ── Load spec from string (for testing) ──────────────────────

bool FigCompletionProvider::load_spec_from_string(
    const std::string &command,
    const std::string &json_str) {
    try {
        nlohmann::json spec = nlohmann::json::parse(json_str);
        std::lock_guard<std::mutex> lock(spec_mutex_);
        specs_[command] = std::move(spec);
        return true;
    } catch (const nlohmann::json::exception &) {
        return false;
    }
}

// ── Load spec from file ──────────────────────────────────────

bool FigCompletionProvider::load_spec(const std::string &command) const {
    {
        std::lock_guard<std::mutex> lock(spec_mutex_);
        if (specs_.find(command) != specs_.end()) {
            return true;  // Already loaded
        }
    }

    auto idx_it = spec_index_.find(command);
    if (idx_it == spec_index_.end()) {
        return false;
    }

    std::ifstream file(idx_it->second);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json spec;
        file >> spec;

        std::lock_guard<std::mutex> lock(spec_mutex_);
        specs_[command] = std::move(spec);
        return true;
    } catch (const nlohmann::json::exception &) {
        return false;
    }
}

// ── Subcommand tree traversal ────────────────────────────────

const nlohmann::json *FigCompletionProvider::find_subcommand(
    const nlohmann::json &node,
    const std::vector<std::string> &args,
    size_t &consumed) const {

    const nlohmann::json *current = &node;
    consumed = 0;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];

        // Skip options (anything starting with -)
        if (!arg.empty() && arg[0] == '-') {
            consumed = i + 1;
            continue;
        }

        // Try to find a matching subcommand
        if (current->contains("subcommands") &&
            (*current)["subcommands"].is_array()) {

            bool found = false;
            for (const auto &sub : (*current)["subcommands"]) {
                if (!sub.contains("name")) continue;

                const auto &name_field = sub["name"];
                bool matches = false;

                if (name_field.is_string()) {
                    matches = (name_field.get<std::string>() == arg);
                } else if (name_field.is_array()) {
                    for (const auto &n : name_field) {
                        if (n.is_string() &&
                            n.get<std::string>() == arg) {
                            matches = true;
                            break;
                        }
                    }
                }

                if (matches) {
                    current = &sub;
                    consumed = i + 1;
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Not a subcommand; treat as a positional arg
                consumed = i + 1;
            }
        } else {
            consumed = i + 1;
        }
    }

    return current;
}

// ── Traverse spec and generate completions ───────────────────

std::vector<Completion> FigCompletionProvider::traverse_and_complete(
    const nlohmann::json &spec,
    const std::string &current_word,
    const std::vector<std::string> &args) const {

    std::vector<Completion> results;

    // Find the deepest matching subcommand
    size_t consumed = 0;
    const nlohmann::json *node = find_subcommand(spec, args, consumed);

    if (!node) {
        return results;
    }

    // Determine what to complete based on current_word
    bool completing_option = !current_word.empty() && current_word[0] == '-';

    if (completing_option) {
        auto opts = extract_options(*node, current_word);
        results.insert(results.end(), opts.begin(), opts.end());
    } else {
        // Complete subcommands
        auto subs = extract_subcommands(*node, current_word);
        results.insert(results.end(), subs.begin(), subs.end());

        // Also complete arguments
        auto arg_comps = extract_arguments(*node, current_word);
        results.insert(results.end(), arg_comps.begin(), arg_comps.end());

        // If current word is empty, also offer options
        if (current_word.empty()) {
            auto opts = extract_options(*node, current_word);
            results.insert(results.end(), opts.begin(), opts.end());
        }
    }

    return results;
}

// ── Extract options from a JSON node ─────────────────────────

std::vector<Completion> FigCompletionProvider::extract_options(
    const nlohmann::json &node,
    const std::string &prefix) const {

    std::vector<Completion> results;

    if (!node.contains("options") || !node["options"].is_array()) {
        return results;
    }

    for (const auto &opt : node["options"]) {
        std::string desc;
        if (opt.contains("description") && opt["description"].is_string()) {
            desc = opt["description"].get<std::string>();
        }

        if (!opt.contains("name")) continue;

        const auto &name_field = opt["name"];
        std::vector<std::string> names;

        if (name_field.is_string()) {
            names.push_back(name_field.get<std::string>());
        } else if (name_field.is_array()) {
            for (const auto &n : name_field) {
                if (n.is_string()) {
                    names.push_back(n.get<std::string>());
                }
            }
        }

        for (const auto &name : names) {
            if (prefix.empty() ||
                name.compare(0, prefix.size(), prefix) == 0) {
                Completion::Type type = Completion::OPTION_LONG;
                if (name.size() == 2 && name[0] == '-' &&
                    name[1] != '-') {
                    type = Completion::OPTION_SHORT;
                }
                results.emplace_back(name, desc, type);
            }
        }
    }

    return results;
}

// ── Extract subcommands from a JSON node ─────────────────────

std::vector<Completion> FigCompletionProvider::extract_subcommands(
    const nlohmann::json &node,
    const std::string &prefix) const {

    std::vector<Completion> results;

    if (!node.contains("subcommands") ||
        !node["subcommands"].is_array()) {
        return results;
    }

    for (const auto &sub : node["subcommands"]) {
        std::string desc;
        if (sub.contains("description") && sub["description"].is_string()) {
            desc = sub["description"].get<std::string>();
        }

        if (!sub.contains("name")) continue;

        const auto &name_field = sub["name"];
        std::vector<std::string> names;

        if (name_field.is_string()) {
            names.push_back(name_field.get<std::string>());
        } else if (name_field.is_array()) {
            for (const auto &n : name_field) {
                if (n.is_string()) {
                    names.push_back(n.get<std::string>());
                }
            }
        }

        for (const auto &name : names) {
            if (prefix.empty() ||
                name.compare(0, prefix.size(), prefix) == 0) {
                results.emplace_back(name, desc, Completion::SUBCOMMAND);
            }
        }
    }

    return results;
}

// ── Extract arguments from a JSON node ───────────────────────

std::vector<Completion> FigCompletionProvider::extract_arguments(
    const nlohmann::json &node,
    const std::string &prefix) const {

    std::vector<Completion> results;

    if (!node.contains("args")) {
        return results;
    }

    const auto &args_field = node["args"];

    // args can be a single object or an array
    std::vector<const nlohmann::json *> arg_nodes;
    if (args_field.is_array()) {
        for (const auto &a : args_field) {
            arg_nodes.push_back(&a);
        }
    } else if (args_field.is_object()) {
        arg_nodes.push_back(&args_field);
    }

    for (const auto *arg_node : arg_nodes) {
        if (arg_node->contains("suggestions") &&
            (*arg_node)["suggestions"].is_array()) {
            for (const auto &sug : (*arg_node)["suggestions"]) {
                std::string text;
                std::string desc;

                if (sug.is_string()) {
                    text = sug.get<std::string>();
                } else if (sug.is_object()) {
                    if (sug.contains("name") &&
                        sug["name"].is_string()) {
                        text = sug["name"].get<std::string>();
                    }
                    if (sug.contains("description") &&
                        sug["description"].is_string()) {
                        desc = sug["description"].get<std::string>();
                    }
                }

                if (!text.empty() && (prefix.empty() ||
                    text.compare(0, prefix.size(), prefix) == 0)) {
                    results.emplace_back(
                        text, desc, Completion::ARGUMENT);
                }
            }
        }
    }

    return results;
}

#endif // TASH_AI_ENABLED
