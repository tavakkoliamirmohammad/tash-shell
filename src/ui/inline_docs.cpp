#include "tash/ui/inline_docs.h"

#include <algorithm>

// ── Built-in flag database ───────────────────────────────────
//
// Maps command name -> (flag -> description).
// Covers the most common POSIX/GNU utilities and git subcommands.

static const std::unordered_map<std::string,
    std::unordered_map<std::string, std::string>> flag_db = {
    {"tar", {
        {"-x", "Extract files from archive"},
        {"-z", "Filter through gzip"},
        {"-f", "Use archive file (next argument)"},
        {"-c", "Create a new archive"},
        {"-v", "Verbose: list files processed"},
        {"-t", "List archive contents"},
        {"--extract", "Extract files from archive"},
        {"--gzip", "Filter through gzip"},
        {"--file", "Use archive file"},
        {"--create", "Create a new archive"},
        {"--verbose", "Verbose output"},
        {"--list", "List contents"},
    }},
    {"grep", {
        {"-i", "Ignore case distinctions"},
        {"-r", "Search recursively"},
        {"-n", "Show line numbers"},
        {"-v", "Invert match (show non-matching)"},
        {"-c", "Count matching lines"},
        {"-l", "List filenames only"},
        {"-E", "Extended regex"},
        {"--color", "Colorize matches"},
        {"--include", "Search only matching files"},
        {"--exclude", "Skip matching files"},
    }},
    {"find", {
        {"-name", "Match filename pattern"},
        {"-type", "Match file type (f=file, d=dir)"},
        {"-size", "Match file size"},
        {"-mtime", "Match modification time (days)"},
        {"-exec", "Execute command on results"},
        {"-delete", "Delete matching files"},
        {"-maxdepth", "Limit directory depth"},
    }},
    {"chmod", {
        {"-R", "Apply recursively"},
        {"--recursive", "Apply recursively"},
    }},
    {"ls", {
        {"-l", "Long listing format"},
        {"-a", "Include hidden files"},
        {"-h", "Human-readable sizes"},
        {"-R", "List recursively"},
        {"-t", "Sort by modification time"},
        {"-S", "Sort by file size"},
    }},
    {"git", {
        {"add", "Stage changes for commit"},
        {"commit", "Record changes to repository"},
        {"push", "Upload local commits to remote"},
        {"pull", "Download and integrate remote changes"},
        {"status", "Show working tree status"},
        {"diff", "Show changes between commits"},
        {"log", "Show commit history"},
        {"branch", "List, create, or delete branches"},
        {"checkout", "Switch branches or restore files"},
        {"merge", "Join two development histories"},
        {"rebase", "Reapply commits on top of another base"},
        {"stash", "Stash changes in dirty working directory"},
        {"clone", "Clone a repository"},
        {"fetch", "Download objects from remote"},
        {"reset", "Reset current HEAD to specified state"},
    }},
};

// ── Built-in command hints ───────────────────────────────────
//
// One-line descriptions for common commands.

static const std::unordered_map<std::string, std::string> cmd_hints = {
    {"tar", "Create or extract compressed archives"},
    {"grep", "Search for patterns in files"},
    {"find", "Search for files in directory hierarchy"},
    {"chmod", "Change file permissions"},
    {"chown", "Change file owner and group"},
    {"ls", "List directory contents"},
    {"cp", "Copy files and directories"},
    {"mv", "Move or rename files"},
    {"rm", "Remove files or directories"},
    {"mkdir", "Create directories"},
    {"cat", "Concatenate and display files"},
    {"head", "Display first lines of a file"},
    {"tail", "Display last lines of a file"},
    {"sed", "Stream editor for text transformation"},
    {"awk", "Pattern scanning and processing"},
    {"curl", "Transfer data with URLs"},
    {"wget", "Download files from the web"},
    {"ssh", "Secure shell remote login"},
    {"scp", "Secure copy over SSH"},
    {"rsync", "Fast incremental file transfer"},
    {"docker", "Container platform"},
    {"git", "Distributed version control system"},
    {"make", "Build automation tool"},
    {"cmake", "Cross-platform build system generator"},
    {"python", "Python interpreter"},
    {"python3", "Python 3 interpreter"},
    {"node", "Node.js JavaScript runtime"},
    {"npm", "Node.js package manager"},
    {"pip", "Python package installer"},
    {"cargo", "Rust package manager and build tool"},
    {"go", "Go programming language tool"},
};

// ── Database access ──────────────────────────────────────────

const std::unordered_map<std::string,
    std::unordered_map<std::string, std::string>> &get_flag_db() {
    return flag_db;
}

const std::unordered_map<std::string, std::string> &get_cmd_hints() {
    return cmd_hints;
}

// ── Helper: try to split combined short flags ────────────────
//
// e.g., "-la" -> {"-l", "-a"}.  Only splits when every character
// after the leading '-' maps to a known single-char flag for the
// given command.  Returns a vector with the original flag if
// splitting is not possible.

static std::vector<std::string> try_split_combined_flags(
    const std::string &arg,
    const std::unordered_map<std::string, std::string> &flags)
{
    // Must start with '-' but not '--', and have 2+ chars after '-'
    if (arg.size() < 3 || arg[0] != '-' || arg[1] == '-') {
        return {arg};
    }

    // Check that every character after '-' is a known single-char flag
    std::vector<std::string> parts;
    for (size_t i = 1; i < arg.size(); ++i) {
        std::string single_flag = std::string("-") + arg[i];
        if (flags.find(single_flag) == flags.end()) {
            // Cannot split -- return original
            return {arg};
        }
        parts.push_back(single_flag);
    }
    return parts;
}

// ── explain_command ──────────────────────────────────────────

std::vector<FlagExplanation> explain_command(
    const std::string &command,
    const std::vector<std::string> &args)
{
    if (args.empty()) {
        return {};
    }

    auto cmd_it = flag_db.find(command);
    if (cmd_it == flag_db.end()) {
        return {};
    }

    const auto &flags = cmd_it->second;
    std::vector<FlagExplanation> result;

    for (const auto &arg : args) {
        // Skip bare positional arguments (not starting with '-' and
        // not a known subcommand like git's)
        auto direct_it = flags.find(arg);
        if (direct_it != flags.end()) {
            FlagExplanation fe;
            fe.flag = arg;
            fe.description = direct_it->second;
            result.push_back(fe);
            continue;
        }

        // Handle long options with = (e.g., --color=auto)
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
            std::string::size_type eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                std::string base_flag = arg.substr(0, eq_pos);
                auto base_it = flags.find(base_flag);
                if (base_it != flags.end()) {
                    FlagExplanation fe;
                    fe.flag = arg;
                    fe.description = base_it->second;
                    result.push_back(fe);
                    continue;
                }
            }
        }

        // Try splitting combined short flags (e.g., -xzf)
        if (arg.size() >= 3 && arg[0] == '-' && arg[1] != '-') {
            auto parts = try_split_combined_flags(arg, flags);
            if (parts.size() > 1) {
                for (const auto &p : parts) {
                    FlagExplanation fe;
                    fe.flag = p;
                    fe.description = flags.at(p);
                    result.push_back(fe);
                }
                continue;
            }
        }

        // If arg starts with '-', it is an unrecognized flag
        if (!arg.empty() && arg[0] == '-') {
            FlagExplanation fe;
            fe.flag = arg;
            fe.description = "";
            result.push_back(fe);
        }
        // else: plain positional argument, skip silently
    }

    return result;
}

// ── get_command_hint ─────────────────────────────────────────

std::string get_command_hint(const std::string &command) {
    auto it = cmd_hints.find(command);
    if (it != cmd_hints.end()) {
        return it->second;
    }
    return "";
}
