# Tash Shell: Plugin Architecture & Feature Design

**Date:** 2026-04-16
**Status:** Approved
**Branches:** 5 parallel feature branches off master

---

## 1. Overview

This design introduces a plugin-compatible architecture for tash-shell that integrates with existing shell ecosystems (Fish completions, Fig/Amazon Q specs, Starship, Atuin) rather than reinventing them. The architecture defines four provider interfaces, with ecosystem adapters as the first implementations that validate the API.

### Goals

- Reuse existing ecosystem plugins/completions (Fish: 1,056 commands, Fig: 715 commands)
- Clean provider interfaces that can later support dynamic loading or Lua scripting
- 5 independent features developed in parallel on separate branches, merged via PRs
- No breaking changes to existing tash behavior

### Non-Goals

- Full Zsh/Oh-My-Zsh plugin execution (requires Zsh interpreter)
- Dynamic plugin loading via dlopen (deferred to future)
- Lua scripting engine (deferred to future)
- Windows support

---

## 2. Core Architecture: Provider Interfaces

### 2.1 Directory Structure

```
src/
  plugins/
    plugin_registry.cpp          # Provider registry and dispatch
    fish_completion_provider.cpp  # Fish completion parser
    fig_completion_provider.cpp   # Fig/Amazon Q JSON spec loader
    sqlite_history_provider.cpp   # SQLite-backed history
    atuin_hook_provider.cpp       # Atuin history bridge
    ai_error_hook_provider.cpp    # AI error recovery
    theme_provider.cpp            # TOML theme system
    starship_prompt_provider.cpp  # Starship prompt integration
include/tash/
  plugin.h                        # Provider interfaces and registry
  plugins/
    fish_completion_provider.h
    fig_completion_provider.h
    sqlite_history_provider.h
    atuin_hook_provider.h
    ai_error_hook_provider.h
    theme_provider.h
    starship_prompt_provider.h
data/
  themes/
    catppuccin-mocha.toml
    catppuccin-latte.toml
    tokyo-night.toml
    dracula.toml
    nord.toml
scripts/
  compile_fig_specs.py            # Fig TypeScript to JSON converter
```

### 2.2 Provider Interfaces (include/tash/plugin.h)

```cpp
#pragma once
#include <string>
#include <vector>
#include <memory>

struct ShellState;  // forward decl

// --- Completion Provider ---

struct Completion {
    std::string text;          // the completion text
    std::string description;   // shown alongside in dropdown
    enum Type { COMMAND, SUBCOMMAND, OPTION_SHORT, OPTION_LONG, ARGUMENT, FILE, DIRECTORY, VARIABLE };
    Type type;
};

class ICompletionProvider {
public:
    virtual ~ICompletionProvider() = default;
    virtual std::string name() const = 0;
    virtual int priority() const = 0;  // higher = preferred when conflicts
    virtual bool can_complete(const std::string &command) const = 0;
    virtual std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const = 0;
};

// --- Prompt Provider ---

class IPromptProvider {
public:
    virtual ~IPromptProvider() = default;
    virtual std::string name() const = 0;
    virtual int priority() const = 0;
    virtual std::string render(const ShellState &state) = 0;
};

// --- History Provider ---

struct HistoryEntry {
    int64_t id;
    std::string command;
    int64_t timestamp;       // unix epoch
    std::string directory;
    int exit_code;
    int duration_ms;
    std::string hostname;
    std::string session_id;
};

struct SearchFilter {
    std::string directory;    // empty = any
    int exit_code = -1;       // -1 = any
    int64_t since = 0;        // 0 = any
    int limit = 50;
};

class IHistoryProvider {
public:
    virtual ~IHistoryProvider() = default;
    virtual std::string name() const = 0;
    virtual void record(const HistoryEntry &entry) = 0;
    virtual std::vector<HistoryEntry> search(
        const std::string &query,
        const SearchFilter &filter) const = 0;
    virtual std::vector<HistoryEntry> recent(int count) const = 0;
};

// --- Hook Provider ---

class IHookProvider {
public:
    virtual ~IHookProvider() = default;
    virtual std::string name() const = 0;
    virtual void on_before_command(const std::string &command, ShellState &state) = 0;
    virtual void on_after_command(
        const std::string &command,
        int exit_code,
        const std::string &stderr_output,
        ShellState &state) = 0;
};

// --- Plugin Registry ---

class PluginRegistry {
public:
    void register_completion_provider(std::unique_ptr<ICompletionProvider> provider);
    void register_prompt_provider(std::unique_ptr<IPromptProvider> provider);
    void register_history_provider(std::unique_ptr<IHistoryProvider> provider);
    void register_hook_provider(std::unique_ptr<IHookProvider> provider);

    // Dispatch methods (called from REPL loop)
    std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const;

    std::string render_prompt(ShellState &state);

    void record_history(const HistoryEntry &entry);
    std::vector<HistoryEntry> search_history(
        const std::string &query,
        const SearchFilter &filter) const;

    void fire_before_command(const std::string &command, ShellState &state);
    void fire_after_command(
        const std::string &command,
        int exit_code,
        const std::string &stderr_output,
        ShellState &state);

private:
    std::vector<std::unique_ptr<ICompletionProvider>> completion_providers_;
    std::vector<std::unique_ptr<IPromptProvider>> prompt_providers_;
    std::vector<std::unique_ptr<IHistoryProvider>> history_providers_;
    std::vector<std::unique_ptr<IHookProvider>> hook_providers_;
};
```

### 2.3 Dispatch Rules

- **Completions:** Query all providers where can_complete() returns true. Merge results. On conflicts (same completion text from multiple providers), prefer higher priority(). Fish = 10, Fig = 20 (richer data), built-in = 5.
- **Prompt:** Use the highest-priority provider. Starship = 20, built-in Catppuccin = 10. User can override in config.
- **History:** Record to ALL registered providers (SQLite + Atuin if both active). Search uses primary (SQLite).
- **Hooks:** Fire ALL registered hooks sequentially. Order: Atuin start hook, then AI error hook on_after.

### 2.4 Registration (in main.cpp)

```cpp
PluginRegistry registry;

// Always register built-in defaults
registry.register_completion_provider(make_unique<BuiltinCompletionProvider>());
registry.register_prompt_provider(make_unique<CatppuccinPromptProvider>(theme));
registry.register_history_provider(make_unique<SqliteHistoryProvider>(db_path));

// Auto-detect ecosystem tools
if (fish_completions_available())
    registry.register_completion_provider(make_unique<FishCompletionProvider>(fish_dirs));
if (fig_specs_available())
    registry.register_completion_provider(make_unique<FigCompletionProvider>(fig_dir));
if (starship_available() && config.prompt == "starship")
    registry.register_prompt_provider(make_unique<StarshipPromptProvider>());
if (atuin_available())
    registry.register_hook_provider(make_unique<AtuinHookProvider>());
if (state.ai_enabled)
    registry.register_hook_provider(make_unique<AiErrorHookProvider>(llm_client));
```

---

## 3. Feature: Fish Completions Provider

**Branch:** feature/fish-completions
**Files:** src/plugins/fish_completion_provider.cpp, include/tash/plugins/fish_completion_provider.h
**Estimated LOC:** ~300
**Dependencies:** None (pure C++ string parsing)

### 3.1 Discovery

Scan these directories for .fish files:

- /usr/share/fish/completions/
- /usr/local/share/fish/completions/
- /opt/homebrew/share/fish/completions/
- ~/.config/fish/completions/

Build an index: map from command name (from filename) to completion file paths.

### 3.2 Parsing

Lazy-load: parse a command's completion file only on first TAB for that command.

Each line matching `^complete\s` is parsed. Extract flags:

| Fish Flag | Meaning | Storage |
|-----------|---------|---------|
| -c cmd | Command name | Key for lookup |
| -s char | Short option | Completion{"-" + char, desc, OPTION_SHORT} |
| -l name | Long option | Completion{"--" + name, desc, OPTION_LONG} |
| -a args | Argument values | Split on spaces, each becomes Completion |
| -d desc | Description | Attached to the completion |
| -f | No file completion | Flag stored, suppresses file completions |
| -r | Requires argument | Flag stored |
| -n cond | Condition (Fish script) | Ignored -- cannot evaluate Fish conditionals |

Quoted strings are handled: `-d "some description"` and `-d 'some description'`.

### 3.3 Cache

Parsed completions are cached in memory (unordered_map). Invalidation: none needed (completion files rarely change). `tash completions reload` clears the cache.

---

## 4. Feature: Fig/Amazon Q Specs Provider

**Branch:** feature/fish-completions (same branch, both are completion providers)
**Files:** src/plugins/fig_completion_provider.cpp, include/tash/plugins/fig_completion_provider.h, scripts/compile_fig_specs.py
**Estimated LOC:** ~500
**Dependencies:** nlohmann/json (already present)

### 4.1 Pre-compilation

The Fig/Amazon Q autocomplete repo contains TypeScript specs. A bundled script converts them to JSON:

```
tash completions update-fig
```

Internally: clone/update the withfig/autocomplete repo, extract the default export objects from TypeScript files, write as JSON to ~/.tash/completions/fig/.

### 4.2 JSON Schema (What We Load)

```json
{
  "name": "git",
  "description": "The content tracker",
  "subcommands": [
    {
      "name": ["checkout", "co"],
      "description": "Switch branches or restore files",
      "options": [
        {
          "name": ["-b", "--branch"],
          "description": "Create and checkout new branch",
          "args": { "name": "new-branch" }
        }
      ],
      "args": { "name": "branch", "isOptional": true }
    }
  ],
  "options": [
    { "name": ["-h", "--help"], "description": "Show help" }
  ]
}
```

### 4.3 Completion Logic

Given `git checkout -<TAB>`:
1. Find spec for "git"
2. Traverse subcommand tree: "git" -> "checkout"
3. Return options at that level

Given `git <TAB>`:
1. Find spec for "git"
2. Return subcommand names with descriptions

---

## 5. Feature: Smart History (SQLite)

**Branch:** feature/smart-history
**Files:** src/plugins/sqlite_history_provider.cpp, include/tash/plugins/sqlite_history_provider.h
**Estimated LOC:** ~400
**Dependencies:** SQLite3 (system, via find_package)

### 5.1 Database

Location: ~/.tash/history.db

Schema:
```sql
CREATE TABLE IF NOT EXISTS history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    command TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    directory TEXT,
    exit_code INTEGER,
    duration_ms INTEGER,
    hostname TEXT,
    session_id TEXT
);
CREATE INDEX IF NOT EXISTS idx_history_command ON history(command);
CREATE INDEX IF NOT EXISTS idx_history_directory ON history(directory);
CREATE INDEX IF NOT EXISTS idx_history_timestamp ON history(timestamp DESC);
```

### 5.2 Recording

Called from main loop after every command execution. Captures command, cwd, exit code, duration, hostname, session UUID (generated at startup). Dedup: skip if previous command in this session is identical.

### 5.3 Search (Ctrl+R)

Override replxx Ctrl+R with custom fuzzy search:
- User types characters -> SQL LIKE query ordered by timestamp DESC
- Results show: command, directory (dimmed), time ago
- Enter selects and inserts into input buffer
- Prefix filters: `dir:` (current directory), `fail:` (non-zero exit), `today:` (last 24h)

### 5.4 New Builtins

- `history` (enhanced): Shows recent commands with timestamps and directories
- `history search <query>`: Full-text search
- `history --here`: Commands run in current directory
- `history --failed`: Commands with non-zero exit
- `history stats`: Most-used commands, directories, failure rate

### 5.5 Migration

On first startup with SQLite enabled, if ~/.tash_history exists:
1. Read each line
2. Insert into SQLite with current timestamp (no directory/exit_code data available)
3. Rename old file to ~/.tash_history.bak

Replxx still manages its own in-memory history for arrow-key navigation. SQLite is the persistent backend.

### 5.6 Atuin Bridge (src/plugins/atuin_hook_provider.cpp, ~80 LOC)

If `atuin` is in PATH, register an IHookProvider:
- on_before_command(): Run `atuin history start`, store returned ID
- on_after_command(): Run `atuin history end` in background (fire-and-forget via fork + exec)

Records to both SQLite (local, fast, always available) and Atuin (synced across machines).

---

## 6. Feature: AI Error Recovery

**Branch:** feature/ai-error-recovery
**Files:** src/plugins/ai_error_hook_provider.cpp, include/tash/plugins/ai_error_hook_provider.h
**Estimated LOC:** ~200
**Dependencies:** Existing AI infrastructure (LLMClient, RateLimiter)

### 6.1 Trigger Conditions

on_after_command() fires an AI call when ALL of:
- exit_code != 0
- exit_code != 130 (Ctrl+C, user-initiated)
- exit_code != 127 (command not found -- already handled by "did you mean?")
- stderr_output is non-empty
- AI is enabled (state.ai_enabled == true)
- Rate limiter allows (max 1 call per 5 seconds)

### 6.2 System Prompt

```
You are a shell error explainer for the tash shell. Given a failed command,
its stderr output, and the working directory, respond with a JSON object:
{
  "explanation": "One sentence explaining what went wrong",
  "fix": "The exact command that would fix the issue, or empty string if no simple fix"
}
Be concise. Only suggest fixes you are confident about.
```

### 6.3 Context Sent

```json
{
  "command": "gcc main.cpp -o main",
  "exit_code": 1,
  "stderr": "main.cpp:5:10: fatal error: 'vector' file not found",
  "directory": "/Users/amir/project",
  "recent_commands": ["cd project", "ls", "gcc main.cpp -o main"]
}
```

### 6.4 Display Format

```
[lightbulb] Missing C++ standard library header. Use g++ instead of gcc for C++ files.
   Fix: g++ main.cpp -o main
   [Enter] run fix  [Esc] dismiss
```

- Explanation in dim yellow
- Fix command in bold green
- Keybinding hint in dim gray
- If no fix suggested, show explanation only

### 6.5 Execution

- Enter: Insert fix command into replxx buffer and execute
- Esc: Dismiss, continue to next prompt
- Timeout: Auto-dismiss after 30 seconds

### 6.6 Configuration

- `@ai errors on` / `@ai errors off` -- toggle (persisted to config)
- Default: on if AI is configured, off otherwise

---

## 7. Feature: Configurable Themes

**Branch:** feature/configurable-themes
**Files:** src/plugins/theme_provider.cpp, include/tash/plugins/theme_provider.h, data/themes/*.toml
**Estimated LOC:** ~200 + ~150 lines of theme files
**Dependencies:** toml11 (header-only, via FetchContent)

### 7.1 Theme File Format

```toml
[meta]
name = "Catppuccin Mocha"
author = "Catppuccin"
variant = "dark"

[syntax]
command_valid = "#a6e3a1"
command_builtin = "#94e2d5"
command_invalid = "#f38ba8"
string = "#f9e2af"
variable = "#89dceb"
operator = "#cba6f7"
redirect = "#fab387"
comment = "#6c7086"

[prompt]
success = "#a6e3a1"
error = "#f38ba8"
path = "#89b4fa"
git_branch = "#f5c2e7"
duration = "#fab387"
user = "#cba6f7"
separator = "#6c7086"

[completion]
builtin = "#94e2d5"
command = "#a6e3a1"
file = "#cdd6f4"
directory = "#89b4fa"
option = "#f9e2af"
description = "#6c7086"
```

### 7.2 Theme Struct

```cpp
struct Theme {
    std::string name;
    std::string variant;  // "dark" or "light"

    // Syntax colors (hex -> parsed to RGB at load time)
    RGB command_valid, command_builtin, command_invalid;
    RGB string_color, variable, op, redirect, comment;

    // Prompt colors
    RGB prompt_success, prompt_error, prompt_path;
    RGB prompt_git, prompt_duration, prompt_user, prompt_separator;

    // Completion colors
    RGB comp_builtin, comp_command, comp_file, comp_directory;
    RGB comp_option, comp_description;

    static RGB parse_hex(const std::string &hex);
};
```

### 7.3 Bundled Themes

| Theme | Variant |
|-------|---------|
| catppuccin-mocha | dark (default) |
| catppuccin-latte | light |
| tokyo-night | dark |
| dracula | dark |
| nord | dark |

### 7.4 Loading

1. Read ~/.tash/config.toml for theme name
2. Search: ~/.tash/themes/<name>.toml, then <install-prefix>/share/tash/themes/<name>.toml
3. Parse TOML, populate Theme struct
4. If not found, fall back to hardcoded Catppuccin Mocha

### 7.5 Integration

Replace all cat_green(), cat_teal() etc. calls in highlight.cpp, prompt.cpp, and completion.cpp with theme struct fields. Theme stored in ShellState.

### 7.6 New Builtins

- `theme list` -- show available themes with color swatch
- `theme set <name>` -- switch theme immediately, save to config
- `theme preview` -- show all theme colors with labels

---

## 8. Feature: Starship Prompt Support

**Branch:** feature/starship-support
**Files:** src/plugins/starship_prompt_provider.cpp, include/tash/plugins/starship_prompt_provider.h
**Estimated LOC:** ~50
**Dependencies:** None (calls starship binary)

### 8.1 Detection

At startup: starship is in PATH AND (starship.toml exists OR STARSHIP_CONFIG env var is set).

### 8.2 Activation

- Auto-detect with config opt-in: `prompt = "starship"` in ~/.tash/config.toml
- Runtime: `prompt set starship` / `prompt set builtin`

### 8.3 Rendering

Calls `starship prompt` with --status, --cmd-duration, --jobs, --terminal-width arguments. Sets STARSHIP_SHELL=bash for compatibility. Returns stdout as prompt string.

---

## 9. Configuration System

All features share: ~/.tash/config.toml

```toml
[theme]
name = "catppuccin-mocha"

[prompt]
provider = "builtin"  # "builtin" or "starship"

[history]
provider = "sqlite"    # "sqlite" or "plain"
atuin_bridge = true    # also record to Atuin if available

[completions]
fish = true            # load Fish completion files
fig = true             # load Fig/Amazon Q specs

[ai]
errors = true          # auto-explain failed commands
contextual = true      # ? suffix and AI completions
completion_delay = 500 # ms before AI completion triggers
provider = "gemini"    # "gemini", "openai", "ollama"

[safety]
trash_rm = true        # mv to trash instead of deleting
warn_destructive = true # warn before dangerous commands

[ui]
blocks = false         # block-style output (opt-in)
block_fold_long = 50   # auto-fold output longer than N lines
clickable_urls = true  # OSC 8 hyperlinks in output
inline_images = true   # Kitty/Sixel image rendering
auto_tables = false    # auto-enhance tabular output
hints = true           # inline documentation hints

[aliases]
you_should_use = true  # remind about existing aliases

[clipboard]
osc52 = true           # use OSC 52 for cross-session clipboard
paste_protection = true # warn on multiline paste

[sync]
remote = ""            # git remote URL for config sync
```

Created with sensible defaults on first run.

---

## 10. Feature: Integrated Fuzzy Finder

**Branch:** feature/fuzzy-finder
**Files:** src/ui/fuzzy_finder.cpp, include/tash/ui/fuzzy_finder.h
**Estimated LOC:** ~500
**Dependencies:** None (built-in, no fzf dependency)

### 10.1 Core Algorithm

Implement a fuzzy matching scorer in C++. Given query "gco" and candidate "git checkout", score based on:
- Consecutive character matches (bonus)
- Matches at word boundaries (bonus: "g" matches start of "git", "c" of "checkout", "o" of "checkout")
- Matches at start of string (bonus)
- Shorter candidates preferred on ties

This is the same algorithm fzf uses (Smith-Waterman variant). ~150 LOC for the scorer.

### 10.2 Keybindings

| Binding | Action | Data Source |
|---------|--------|------------|
| Ctrl+R | Fuzzy history search | SQLite history (or replxx history if no SQLite) |
| Ctrl+T | Fuzzy file finder | Recursive directory listing (respects .gitignore) |
| Ctrl+G | Fuzzy git browser | git branch, git log --oneline, git status |
| Alt+C | Fuzzy directory jump | z frecency database + recent directories from history |
| Ctrl+P | Command palette | All tash builtins, aliases, theme commands, @ai commands |

### 10.3 TUI Rendering

Uses replxx's hint/completion area or raw terminal escape codes:
```
> query_text
  3/147
> matching result 1 (highlighted)
  matching result 2
  matching result 3
  ...
```

- Live filtering as user types
- Up/Down to navigate results
- Enter to select and insert into command line
- Esc to cancel
- Tab to toggle multi-select (for Ctrl+T file finder)

### 10.4 File Finder (Ctrl+T)

- Recursively lists files from cwd
- Respects .gitignore if in a git repo (parse .gitignore or shell out to `git ls-files`)
- Selected file path inserted at cursor position in command line
- Multi-select with Tab: inserts all selected paths space-separated

### 10.5 Git Browser (Ctrl+G)

Three modes, cycled with Ctrl+G repeated presses:
1. **Branches:** `git branch -a` -- select inserts branch name
2. **Log:** `git log --oneline -50` -- select inserts commit hash
3. **Status:** `git status --short` -- select inserts file path

---

## 11. Feature: Command Safety Net

**Branch:** feature/command-safety
**Files:** src/plugins/safety_hook_provider.cpp, include/tash/plugins/safety_hook_provider.h
**Estimated LOC:** ~250
**Dependencies:** None

### 11.1 Dangerous Command Detection

Implements IHookProvider::on_before_command(). Checks command against a pattern list before execution:

| Pattern | Risk | Warning |
|---------|------|---------|
| `rm -rf /` or `rm -rf /*` | CRITICAL | "This will delete your entire filesystem. Blocked." |
| `rm -rf <path>` (>10 files) | HIGH | "This will recursively delete <path> (<N> items). Proceed? [y/N]" |
| `rm -r <path>` | MEDIUM | "Recursive delete of <path>. Proceed? [y/N]" |
| `chmod -R 777` | HIGH | "This grants full permissions recursively. Proceed? [y/N]" |
| `chmod -R` | MEDIUM | "Recursive permission change. Proceed? [y/N]" |
| `git push --force` | HIGH | "Force push will overwrite remote history. Proceed? [y/N]" |
| `git reset --hard` | HIGH | "This discards all uncommitted changes. Proceed? [y/N]" |
| `dd if=` | HIGH | "dd writes directly to device. Verify target. Proceed? [y/N]" |
| `mkfs` | CRITICAL | "This formats a filesystem. All data will be lost. Proceed? [y/N]" |
| `> <existing-file>` | MEDIUM | "This will truncate <file>. Proceed? [y/N]" |

### 11.2 Trash-Based rm

Optional behavior (configured in config.toml):

```toml
[safety]
trash_rm = true   # mv to ~/.tash/trash instead of deleting
warn_destructive = true
```

When `trash_rm = true`:
- `rm file.txt` moves to `~/.tash/trash/<timestamp>-file.txt`
- `rm -rf dir/` moves to `~/.tash/trash/<timestamp>-dir/`
- Original path stored in `~/.tash/trash/.manifest` for undo
- `trash list` -- show trashed items with original paths and timestamps
- `trash restore <item>` -- restore to original location
- `trash empty` -- permanently delete trash contents
- `trash empty --older 30d` -- delete items older than 30 days
- Bypass with `\rm` (backslash escapes the hook)

### 11.3 Dry-Run Suggestions

When a command is detected as potentially destructive and has a `--dry-run` or `-n` equivalent, suggest it:
```
$ rsync --delete /src/ /dst/
⚠️  --delete removes files from destination. Consider: rsync --delete --dry-run /src/ /dst/
   [Enter] run as-is  [d] run dry-run first  [Esc] cancel
```

Known dry-run flags stored as a static map: rsync (-n), make (-n), pip install (--dry-run), npm (--dry-run), terraform (plan), etc.

---

## 12. Feature: Contextual AI (Seamless Mode)

**Branch:** feature/contextual-ai
**Files:** src/ai/contextual_ai.cpp, include/tash/ai/contextual_ai.h
**Estimated LOC:** ~350
**Dependencies:** Existing AI infrastructure

### 12.1 The ? Suffix Operator

Type a natural language description ending with `?` and tash generates the command:

```
$ find all python files larger than 1MB?
🔍 find . -name "*.py" -size +1M
   [Enter] run  [e] edit  [Esc] cancel
```

**Detection:** In the REPL loop, after the user presses Enter, if the input ends with `?` AND does not parse as a valid command, route to AI instead of executing.

**Flow:**
1. User types `compress all logs older than 7 days?`
2. Tash detects: ends with `?`, not a valid command
3. Sends to LLM with context: cwd, recent commands, OS
4. LLM returns the command
5. Display with run/edit/cancel prompt
6. On [e], inserts command into replxx buffer for editing

### 12.2 AI-Powered Completions

When the user is typing and pauses for >500ms on a partial natural language phrase (heuristic: contains spaces and no valid command prefix), show an AI-generated ghost suggestion:

```
$ find large log files_                      ← cursor here, 500ms pause
  find . -name "*.log" -size +100M           ← ghost text (dim)
```

- Right arrow accepts the ghost suggestion (same as history autosuggestion)
- Any other key continues normal typing
- Only triggers if AI is enabled and rate limiter allows
- Uses a lightweight/fast model (Gemini Flash or Ollama local) to minimize latency
- Cached: same prefix reuses previous suggestion

### 12.3 Context Accumulation

AI calls include rolling context (stored in ShellState):
- Current working directory
- Last 5 commands and their exit codes
- Current git branch (if in repo)
- Detected project type (package.json = Node, Cargo.toml = Rust, CMakeLists.txt = C++, etc.)
- Last stderr output (if recent command failed)

This context is sent as a JSON preamble in the system prompt, not as conversation history (keeps token count low).

### 12.4 Configuration

```toml
[ai]
contextual = true       # enable ? suffix and AI completions
completion_delay = 500  # ms before AI completion triggers
```

`@ai` prefix continues to work for explicit conversational queries.

---

## 13. Feature: Pipeline Builder

**Branch:** feature/pipeline-builder
**Files:** src/core/structured_pipe.cpp, include/tash/core/structured_pipe.h
**Estimated LOC:** ~600
**Dependencies:** nlohmann/json (already present)

### 13.1 The |> Operator

A new pipe operator `|>` for structured (JSON) data flow. Traditional `|` remains unchanged (text pipes, full POSIX).

```bash
# Traditional pipe (unchanged)
ls -la | grep ".cpp" | wc -l

# Structured pipe
ls |> where size > 1mb |> sort-by modified |> select name size
```

### 13.2 How It Works

1. **Left side of |>:** Command runs normally, but stdout is captured
2. **Auto-detection:** If output is valid JSON, parse it. If not, convert text lines to a JSON array of objects (split by whitespace into columns)
3. **Right side of |>:** Built-in structured operators process the JSON data
4. **Final output:** Rendered as a formatted table or raw JSON

### 13.3 Built-in Structured Operators

| Operator | Example | Description |
|----------|---------|-------------|
| `where` | `where size > 1mb` | Filter rows by condition |
| `sort-by` | `sort-by name` | Sort by column |
| `select` | `select name size` | Pick columns |
| `reject` | `reject permissions` | Remove columns |
| `first` | `first 5` | Take first N rows |
| `last` | `last 10` | Take last N rows |
| `count` | `count` | Count rows |
| `uniq` | `uniq` | Remove duplicate rows |
| `group-by` | `group-by extension` | Group rows by column value |
| `to-json` | `to-json` | Output raw JSON |
| `to-csv` | `to-csv` | Output as CSV |
| `to-table` | `to-table` | Output as formatted table (default) |

### 13.4 Smart Wrappers for Common Commands

When commands are on the left side of `|>`, tash provides structured output automatically:

| Command | Structured Output |
|---------|------------------|
| `ls` | `[{"name": "file.cpp", "size": 4096, "modified": "...", "type": "file"}]` |
| `ps` | `[{"pid": 1234, "cpu": 0.5, "mem": 1.2, "command": "..."}]` |
| `docker ps` | `[{"id": "abc", "image": "nginx", "status": "Up 2h", "ports": "80/tcp"}]` |
| `git log` | `[{"hash": "abc", "author": "...", "date": "...", "message": "..."}]` |
| `git status` | `[{"file": "main.cpp", "status": "modified"}]` |
| `df` | `[{"filesystem": "/dev/sda1", "size": "100G", "used": "50G", "avail": "50G"}]` |

These wrappers parse the text output of the real commands into JSON. Only active when followed by `|>`.

### 13.5 Table Rendering

Default output format for `|>` chains is a formatted table:

```
$ ps |> where cpu > 1.0 |> sort-by cpu |> select pid command cpu
┌──────┬─────────────┬──────┐
│ pid  │ command     │ cpu  │
├──────┼─────────────┼──────┤
│ 3421 │ node        │ 12.3 │
│ 1892 │ chrome      │  5.7 │
│ 4501 │ cargo build │  3.2 │
└──────┴─────────────┴──────┘
```

Uses Unicode box-drawing characters. Colors from active theme. Falls back to ASCII on non-Unicode terminals.

### 13.6 JSON-in, JSON-out

If stdin to the first command in a `|>` chain is already JSON (e.g., from curl), it's parsed directly:

```bash
curl -s api.example.com/users |> where active == true |> select name email |> first 5
```

---

## 14. Feature: Block-Style Output

**Branch:** feature/block-output
**Files:** src/ui/block_renderer.cpp, include/tash/ui/block_renderer.h
**Estimated LOC:** ~400
**Dependencies:** None (terminal escape sequences)

### 14.1 What a Block Is

Each command execution produces a discrete "block" containing:
- The command that was run
- Its output (stdout + stderr)
- Metadata: exit code, duration, timestamp

Blocks are visually separated by thin horizontal lines using box-drawing characters.

### 14.2 Visual Format

```
─── git status ──────────────────────────── 0.02s ✓ ───
On branch master
Changes not staged for commit:
  modified:   src/main.cpp

─── make -j8 ────────────────────────────── 3.41s ✗ ───
src/main.cpp:42:5: error: expected ';'
make: *** [main.o] Error 1

─── ▊                                                 ───
```

- Header: command name (bold) + duration (dim) + status icon (green check / red x)
- Body: command output, indented
- Failed blocks: red-tinted header
- Active block (current input): blinking cursor

### 14.3 Block Navigation

| Keybinding | Action |
|-----------|--------|
| Alt+Up | Jump to previous block header |
| Alt+Down | Jump to next block header |
| Alt+C | Copy current block's output to clipboard (OSC 52) |
| Alt+F | Fold/collapse current block (hide output, show header only) |
| Alt+E | Expand all folded blocks |

### 14.4 Implementation Approach

Blocks are rendered using ANSI escape sequences. The shell tracks block boundaries by recording cursor position before and after each command. Block headers are drawn using the prompt rendering system.

Key challenge: output from long-running commands streams in real-time, so the block "closes" (draws the bottom separator and metadata) only after the command completes.

### 14.5 Configuration

```toml
[ui]
blocks = true         # enable block-style output
block_fold_long = 50  # auto-fold output longer than 50 lines
```

Disabled by default until stable. Enable with `tash set blocks on`.

---

## 15. Feature: Smart Aliases & Workflows

**Branch:** feature/smart-aliases
**Files:** src/plugins/alias_suggest_provider.cpp, include/tash/plugins/alias_suggest_provider.h
**Estimated LOC:** ~200
**Dependencies:** SQLite history (for frequency analysis)

### 15.1 "You Should Use" Reminders

Implements IHookProvider::on_before_command(). When the user types a command that matches an existing alias, show a reminder:

```
$ git checkout main
💡 You have an alias for this: gco main

$ docker compose up -d
💡 You have an alias for this: dcu
```

Only triggers for exact alias matches. Shows once per session per alias (doesn't nag).

### 15.2 AI Alias Suggestions

New builtin `alias suggest` analyzes command history and proposes aliases:

```
$ alias suggest
Based on your last 1000 commands:

  alias gst='git status'           # used 47 times
  alias gp='git push'              # used 31 times
  alias dc='docker compose'        # used 28 times
  alias k='kubectl'                # used 22 times

  Add all? [y/N] or select individually [1-4]:
```

Uses SQLite history to count command frequencies. Groups by command prefix. Suggests aliases for the top N most-repeated commands that don't already have aliases.

### 15.3 Built-in Alias Packs

Ship curated alias sets that users can opt into:

```
$ alias pack list
  git       - 15 aliases (gst, gco, gp, gl, ...)
  docker    - 12 aliases (dps, dcu, dcd, ...)
  kubectl   - 10 aliases (k, kgp, kgs, ...)
  npm       - 8 aliases (ni, nr, nb, nt, ...)

$ alias pack enable git
✓ Enabled 15 git aliases. Run 'alias pack show git' to see them.
```

Alias packs stored as TOML files in `data/alias-packs/`.

---

## 16. Feature: Inline Documentation

**Branch:** feature/inline-docs
**Files:** src/ui/inline_docs.cpp, include/tash/ui/inline_docs.h
**Estimated LOC:** ~300
**Dependencies:** None (shells out to tldr if available)

### 16.1 Hint Text (Murex-style)

As the user types a command and pauses (300ms), show a dim one-liner below the input describing what the command does:

```
$ tar -xzf_
  Extract a gzipped tar archive
```

Data sources (in priority order):
1. Fig/Amazon Q specs (already loaded -- use the `description` field)
2. Fish completion descriptions
3. tldr pages (if `tldr` binary is installed -- cache the one-liner summaries)
4. Fall back to nothing (no hint)

### 16.2 Explain Builtin

New builtin `explain` that breaks down a command into its parts:

```
$ explain tar -xzf archive.tar.gz
  tar           Extract/create tape archives
  -x            Extract files from archive
  -z            Filter through gzip
  -f            Use archive file (next argument)
  archive.tar.gz  The archive to extract
```

Uses Fish/Fig completion data to resolve flag descriptions. For unknown flags, falls back to AI if available.

### 16.3 Flag Descriptions in Completions

When tab-completing flags, show descriptions inline:

```
$ grep --<TAB>
  --color          Surround matches with color escape sequences
  --count          Print only a count of matching lines
  --extended-regexp  Interpret patterns as extended regex
  --fixed-strings   Interpret pattern as fixed strings
  ...
```

This is already partially supported by the Fish/Fig completion providers (they have descriptions). This feature ensures the completion UI renders those descriptions prominently.

---

## 17. Feature: Rich Output

**Branch:** feature/rich-output
**Files:** src/ui/rich_output.cpp, include/tash/ui/rich_output.h
**Estimated LOC:** ~350
**Dependencies:** None (terminal protocol support)

### 17.1 Clickable URLs (OSC 8)

Detect URLs in command output and wrap them with OSC 8 hyperlink escape sequences:

```
Before: Visit https://github.com/user/repo for details
After:  Visit [https://github.com/user/repo](clickable) for details
```

URL detection via regex: `https?://[^\s<>\"]+`. Wrap with `\e]8;;URL\e\\TEXT\e]8;;\e\\`.

Terminals that support OSC 8: iTerm2, Kitty, WezTerm, Ghostty, GNOME Terminal (VTE), Windows Terminal. Others show plain text (graceful fallback).

### 17.2 Inline Images (Kitty Graphics Protocol)

Support displaying images inline when using compatible terminals:

```
$ tash image show photo.png
[inline image rendered in terminal]

$ tash image show *.png --grid
[thumbnail grid of all matching images]
```

New builtin `image show` that:
1. Detects terminal graphics support (query via `\e_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\e\\` and check response)
2. If Kitty protocol supported: encode image as base64, send via Kitty escape sequence
3. If Sixel supported: convert to Sixel format
4. If neither: show file info only (dimensions, size, type)

### 17.3 Table Auto-Detection

When command output looks like a table (aligned columns with consistent spacing), auto-enhance with box-drawing:

```
# Before (raw ps output):
PID   CPU  MEM  COMMAND
1234  5.2  1.0  node
5678  2.1  0.5  python

# After (auto-enhanced):
┌──────┬─────┬─────┬─────────┐
│ PID  │ CPU │ MEM │ COMMAND │
├──────┼─────┼─────┼─────────┤
│ 1234 │ 5.2 │ 1.0 │ node    │
│ 5678 │ 2.1 │ 0.5 │ python  │
└──────┴─────┴─────┴─────────┘
```

Heuristic: if first line looks like a header (all caps or title case) and subsequent lines align to the same column positions, render as a table. Disabled when piped (respects pipe semantics).

### 17.4 Configuration

```toml
[ui]
clickable_urls = true
inline_images = true
auto_tables = false     # off by default, opt-in (can be surprising)
```

---

## 18. Feature: Session Persistence

**Branch:** feature/session-persistence
**Files:** src/core/session.cpp, include/tash/core/session.h
**Estimated LOC:** ~400
**Dependencies:** None

### 18.1 How It Works

A lightweight alternative to tmux for simple session recovery:

```bash
# Start a persistent session
$ tash --persist mywork

# Detach (like tmux Ctrl+B d)
# Press Ctrl+\ or close terminal

# Reattach from any terminal
$ tash --attach mywork

# List active sessions
$ tash --sessions
  mywork   /Users/amir/project   2h ago   detached
  debug    /Users/amir/logs      30m ago  attached (pts/3)
```

### 18.2 What Gets Persisted

| State | Persisted | How |
|-------|-----------|-----|
| Working directory | Yes | Saved to session file |
| Environment variables | Yes | Exported vars saved |
| Aliases | Yes | Current alias table saved |
| Command history | Yes | Already in SQLite |
| Background jobs | No | Processes die on detach (use nohup) |
| Terminal state | No | Fresh terminal on reattach |
| Scrollback | No | Not saved (too large) |

### 18.3 Implementation

Uses a Unix domain socket per session:
1. `tash --persist name` creates `~/.tash/sessions/name.sock` and forks a daemon
2. The daemon runs the actual shell REPL, reading input from the socket
3. `tash --attach name` connects to the socket, proxies terminal I/O
4. On detach (socket close), the daemon keeps running
5. On reattach, the daemon sends current state (cwd, env) and resumes

### 18.4 Session Cleanup

- `tash --kill name` terminates a session daemon
- Sessions auto-terminate after 24h of inactivity (configurable)
- `~/.tash/sessions/` cleaned on `tash --sessions --gc`

---

## 19. Feature: Startup Optimization

**Branch:** feature/startup-optimization
**Files:** Modifications across main.cpp, CMakeLists.txt
**Estimated LOC:** ~150 (mostly refactoring)
**Dependencies:** None

### 19.1 Benchmarking

Add `tash --benchmark` flag:
```
$ tash --benchmark
Startup breakdown:
  Binary load:       2.1ms
  Config parse:      1.3ms
  History load:      4.2ms
  Plugin discovery:  3.1ms
  Completion index:  2.8ms
  Prompt render:     0.9ms
  ──────────────────────────
  Total:            14.4ms
```

Uses `std::chrono::high_resolution_clock` at each stage.

### 19.2 Optimization Strategies

1. **Lazy completion loading:** Don't parse Fish/Fig files at startup. Build filename index only (scan directory, store map of command->path). Parse on first TAB.

2. **Config caching:** Parse TOML config once, write a binary cache (~/.tash/config.cache). On subsequent starts, check mtime of config.toml -- if unchanged, load binary cache (faster than TOML parse).

3. **History lazy-load:** Open SQLite connection at startup but don't query. First Ctrl+R or `history` command triggers initial load.

4. **Parallel initialization:** Use std::async for independent init tasks (plugin discovery, history open, config parse).

5. **Compile-time optimization:** LTO (Link Time Optimization) in release builds. Strip debug symbols.

### 19.3 Targets

| Metric | Target | Measured Against |
|--------|--------|-----------------|
| Cold start (first ever) | <100ms | Fish 4.0: ~80ms |
| Warm start (cached) | <50ms | Bash: ~5ms |
| First completion | <200ms | Including lazy Fish parse |
| Prompt render | <10ms | Starship target |

### 19.4 CI Benchmark

Add a GitHub Actions step that measures startup time on each push and fails if it regresses beyond threshold (e.g., >150ms).

---

## 20. Feature: Clipboard Integration (OSC 52)

**Branch:** feature/clipboard
**Files:** src/ui/clipboard.cpp, include/tash/ui/clipboard.h
**Estimated LOC:** ~150
**Dependencies:** None (terminal escape sequences)

### 20.1 Problem

75 people in Julia Evans' survey cited clipboard as a major frustration. Copying over SSH fails. Multiple competing clipboard systems (system, tmux, X11). Random spaces appear when copying.

### 20.2 OSC 52 Protocol

OSC 52 is the terminal escape sequence for clipboard access. Works over SSH, inside tmux, inside screen -- anywhere the terminal supports it.

Write to clipboard:
```
\e]52;c;<base64-encoded-text>\a
```

Read from clipboard (if terminal allows):
```
\e]52;c;?\a
```

Supported by: iTerm2, Kitty, WezTerm, Ghostty, Alacritty, tmux (with `set -g set-clipboard on`), Windows Terminal, foot, GNOME Terminal (VTE 0.76+).

### 20.3 Shell Integration

**New builtins:**
- `copy` -- copy text to clipboard via OSC 52
  ```bash
  echo "hello" | copy          # pipe to clipboard
  copy "literal text"          # copy literal string
  copy --file path.txt         # copy file contents
  ```
- `paste` -- paste from clipboard (where supported by terminal)
  ```bash
  paste                        # print clipboard contents
  paste | grep pattern         # pipe clipboard into command
  ```

**Block integration:** Alt+C in block mode copies the selected block's output via OSC 52.

**History integration:** `history copy <id>` copies a specific history entry to clipboard.

### 20.4 Bracketed Paste Protection

Replxx already supports bracketed paste mode. Enhance with:
- Visual indicator when multiline paste is detected: "Pasting 5 lines. Execute? [y/N/e]"
- [e] opens the pasted content in replxx for editing before execution
- Configurable: `[safety] paste_protection = true`

### 20.5 Fallback

If terminal doesn't support OSC 52, fall back to:
- macOS: `pbcopy`/`pbpaste`
- Linux: `xclip` or `xsel` or `wl-copy`/`wl-paste`
- If none available: print error with install instructions

---

## 21. Feature: Man Page Completion Generation

**Branch:** feature/manpage-completions
**Files:** src/plugins/manpage_completion_provider.cpp, include/tash/plugins/manpage_completion_provider.h, scripts/generate_manpage_completions.py
**Estimated LOC:** ~250 (C++) + ~200 (Python)
**Dependencies:** Python 3 (for generation script), man pages installed

### 21.1 Why

Fish's 1,056 completion files don't cover every command. Man pages exist for nearly every installed command. Parsing man pages fills the gaps that Fish and Fig don't cover.

### 21.2 Two-Stage Approach

**Stage 1: Reuse Fish's Python script (fast path)**
Fish ships `create_manpage_completions.py` (~1,600 lines). Bundle a modified version that:
1. Scans `/usr/share/man/man1/*.gz` and `/usr/local/share/man/man1/*.gz`
2. Parses groff/troff formatting (deroffer)
3. Extracts options from OPTIONS/DESCRIPTION sections
4. Outputs completion data as JSON (instead of Fish `complete` format)
5. Writes to `~/.tash/completions/manpage/<command>.json`

Run via: `tash completions generate` (one-time, takes 30-60s for all man pages)

**Stage 2: --help parsing (lightweight fallback)**
For commands without man pages, parse `<command> --help` output:
1. Run `<command> --help 2>&1` with 2s timeout
2. Regex extract lines matching `-X, --long-flag  Description text`
3. Common patterns: `-f, --file FILE`, `--verbose`, `-n NUM`
4. Cache results to `~/.tash/completions/help/<command>.json`

Run lazily: first time user tabs on an unknown command, try `--help` parse.

### 21.3 Integration

Registers as an `ICompletionProvider` with priority 5 (lowest -- Fish and Fig take precedence when available). Only consulted when neither Fish nor Fig has completions for a command.

### 21.4 Cache Invalidation

- Man page completions regenerated via `tash completions generate --force`
- Help-based completions invalidated after 30 days (check file mtime)
- `tash completions status` shows coverage: "1,056 Fish + 715 Fig + 342 manpage + 89 help = 2,202 commands"

---

## 22. Feature: Config Sync

**Branch:** feature/config-sync
**Files:** src/core/config_sync.cpp, include/tash/core/config_sync.h
**Estimated LOC:** ~300
**Dependencies:** Git (for sync mechanism)

### 22.1 Problem

Developers use multiple machines. Shell config drifts between them. Tools like chezmoi exist but add another layer of complexity.

### 22.2 Git-Based Sync

Tash's `~/.tash/` directory is the config home. Make it sync-able via git:

```bash
# Initialize sync (one-time setup)
$ tash sync init
Initialized git repo at ~/.tash/
Add a remote: tash sync remote <url>

# Set remote
$ tash sync remote git@github.com:user/tash-config.git

# Push config
$ tash sync push
Committed and pushed: config.toml, themes/, alias-packs/, completions/fig/

# Pull on another machine
$ tash sync pull
Updated config from remote. Restart tash to apply.
```

### 22.3 What Syncs

| Path | Syncs | Why |
|------|-------|-----|
| `config.toml` | Yes | Theme, provider choices, feature flags |
| `themes/*.toml` | Yes | Custom themes |
| `alias-packs/*.toml` | Yes | Custom alias packs |
| `completions/fig/*.json` | Optional | Large, can be regenerated |
| `history.db` | No | Too large, use Atuin for history sync |
| `sessions/` | No | Machine-local |
| `trash/` | No | Machine-local |
| `cache/` | No | Machine-local, regenerated |

### 22.4 Conflict Resolution

- TOML files: pull warns on conflicts, user resolves manually
- Auto-merge for additive changes (new aliases, new theme files)
- `tash sync diff` shows what would change before pulling

### 22.5 .tashrc Sync

The existing `~/.tashrc` is separate from `~/.tash/`. Option to symlink: `tash sync link-tashrc` creates `~/.tashrc -> ~/.tash/tashrc` so the rc file syncs too.

---

## 23. Feature: Light/Dark Auto-Detection

**Branch:** feature/configurable-themes (extend existing theme branch)
**Files:** Addition to src/plugins/theme_provider.cpp
**Estimated LOC:** ~50 (addition to theme provider)

### 23.1 Detection Protocol

Query the terminal's background color via OSC 11:
```
Send:    \e]11;?\a
Receive: \e]11;rgb:RRRR/GGGG/BBBB\a
```

Parse the RGB response. If luminance > 0.5, it's a light background. If <= 0.5, it's a dark background.

Supported by: iTerm2, Kitty, WezTerm, Ghostty, xterm, GNOME Terminal, foot, Windows Terminal.

### 23.2 Auto Theme Selection

In config.toml:
```toml
[theme]
name = "auto"  # special value: auto-detect
dark = "catppuccin-mocha"
light = "catppuccin-latte"
```

When `name = "auto"`:
1. Query terminal background via OSC 11
2. If response received within 100ms, select dark or light theme
3. If no response (terminal doesn't support), default to dark

### 23.3 Runtime Switching

If the user changes their terminal theme (e.g., macOS auto dark mode), tash can re-detect on the next prompt render. Check once per minute (not every prompt -- too expensive).

---

## 24. Feature: Output Sharing

**Branch:** feature/rich-output (extend existing branch)
**Files:** Addition to block renderer
**Estimated LOC:** ~100 (addition to block output)

### 24.1 Export Block as Text

```bash
$ tash export last          # copy last block's output
$ tash export last --ansi   # include colors (for sharing to another terminal)
$ tash export last --html   # render as HTML with syntax colors
$ tash export last --md     # render as markdown code block
```

### 24.2 Share via Gist

If `gh` CLI is available:
```bash
$ tash export last --gist
Created gist: https://gist.github.com/user/abc123
(URL copied to clipboard)
```

### 24.3 Save Session Log

```bash
$ tash log start           # start recording all commands + output
$ tash log stop            # stop recording
$ tash log save session.md # export as markdown with command blocks
```

Useful for creating documentation, bug reports, or tutorials from real terminal sessions.

---

## 25. Build System Changes

### CMakeLists.txt Additions

- SQLite3: find_package, conditional compile with TASH_SQLITE_ENABLED
- toml11: FetchContent from GitHub (header-only, v4.2.0)
- Plugin source files always compiled (fish, fig, theme, starship, registry, clipboard, manpage)
- SQLite-dependent sources conditional (sqlite_history, atuin_hook, alias_suggest)
- AI-dependent sources conditional (ai_error_hook, contextual_ai)

### New Dependencies

| Dependency | Method | Required? |
|-----------|--------|-----------|
| SQLite3 | find_package | Optional (history, aliases) |
| toml11 v4.2.0 | FetchContent | Required (config, themes) |
| Python 3 | System | Optional (man page generation, Fig compile) |

### Graceful Degradation

| Dependency | Missing | Behavior |
|-----------|---------|----------|
| SQLite3 | Not found | Fall back to plain-text history, no alias suggest |
| Fish completions dir | Not found | Skip, use built-in only |
| Fig JSON cache | Not found | Skip Fig completions |
| Man pages | Not found | Skip manpage completions |
| Python 3 | Not found | Cannot generate manpage completions or compile Fig specs |
| Starship binary | Not found | Use built-in prompt |
| Atuin binary | Not found | Skip Atuin bridge |
| OpenSSL (AI) | Not found | Skip AI error recovery, contextual AI, explain AI fallback |
| tldr binary | Not found | Skip tldr hints, use Fish/Fig descriptions only |
| gh CLI | Not found | Skip gist export |
| OSC 52 support | Not supported | Fall back to pbcopy/xclip |
| OSC 11 support | Not supported | Default to dark theme |
| Kitty/Sixel | Not supported | Skip inline images |

Tash always works. Ecosystem integrations enhance when available.

---

## 26. Comprehensive Testing Strategy

Tests integrate with the existing GoogleTest (v1.14.0) framework. Currently 275 tests across 2 unit binaries (test_tokenizer, test_ai) and 1 integration binary (test_integration with 15 source files). New tests follow the same patterns: `EXPECT_*`/`ASSERT_*` assertions, `ShellResult run_shell()` for integration tests, fixtures for state isolation.

### 26.1 Plugin Core Tests

**File:** `tests/unit/test_plugin_registry.cpp`
**Binary:** `test_plugin_registry` (new, links shell_lib + GTest)

| Test | What It Verifies |
|------|-----------------|
| RegisterCompletionProvider | Provider added, retrievable |
| RegisterMultipleProviders | Multiple providers coexist |
| CompletionPriorityOrdering | Higher priority provider's results come first |
| CompletionMergeNoDuplicates | Same completion from 2 providers deduped by priority |
| CompletionCanCompleteFilter | Only providers returning can_complete=true are queried |
| PromptHighestPriorityWins | Highest-priority prompt provider renders |
| PromptFallbackOnFailure | If primary fails, next provider renders |
| HistoryRecordToAll | Recording goes to all registered history providers |
| HistorySearchPrimary | Search uses first registered provider |
| HookFiresAllBeforeCommand | All hook providers fire on_before_command |
| HookFiresAllAfterCommand | All hook providers fire on_after_command |
| HookOrderPreserved | Hooks fire in registration order |
| EmptyRegistryNoError | Dispatch methods work with no providers registered |

### 26.2 Fish Completion Provider Tests

**File:** `tests/unit/test_fish_completion.cpp`
**Binary:** `test_fish_completion` (new)

| Test | What It Verifies |
|------|-----------------|
| ParseSimpleComplete | `complete -c git -s b -l branch -d "desc"` parsed correctly |
| ParseShortOption | `-s f` produces Completion{"-f", ..., OPTION_SHORT} |
| ParseLongOption | `-l verbose` produces Completion{"--verbose", ..., OPTION_LONG} |
| ParseOldStyleOption | `-o foo` produces Completion{"-foo", ..., OPTION_LONG} |
| ParseDescription | `-d "some text"` extracts description |
| ParseSingleQuotedDesc | `-d 'some text'` handles single quotes |
| ParseArguments | `-a "start stop restart"` produces 3 argument completions |
| ParseNoFileFlag | `-f` sets suppress_files flag |
| ParseRequiresArgFlag | `-r` sets requires_argument flag |
| ParseMultipleFlagsOneLine | `complete -c git -s b -l branch -d "desc" -r` all extracted |
| IgnoreConditionFlag | `-n '__fish_seen_command'` silently ignored |
| IgnoreWrapFlag | `-w other_cmd` silently ignored |
| ParseEmptyLine | Empty or whitespace-only lines skipped |
| ParseCommentLine | Lines starting with # skipped |
| ParseMalformedLine | Missing -c flag -> line skipped, no crash |
| ParseUnterminatedQuote | `-d "unterminated` -> handled gracefully |
| LazyLoadOnFirstTab | Completion file parsed only on first can_complete/complete call |
| CacheHitOnSecondTab | Second call returns cached results without re-parsing |
| DiscoveryFindsSystemDir | Finds /usr/share/fish/completions/ |
| DiscoveryFindsHomeDir | Finds ~/.config/fish/completions/ |
| DiscoveryMissingDirNoError | Non-existent directory silently skipped |
| CompletesGitSubcommands | `git <TAB>` returns checkout, commit, push, etc. with descriptions |
| CompletesGrepFlags | `grep --<TAB>` returns --color, --count, etc. |

**File:** `tests/integration/test_fish_completion.cpp`

| Test | What It Verifies |
|------|-----------------|
| TabCompletionShowsDescription | git TAB in shell shows subcommands with description text |
| CompletionColoredCorrectly | Subcommand completions use correct theme color |
| FallsBackToBuiltinWhenNoFish | Without Fish dir, git completions still work (built-in) |

### 26.3 Fig Completion Provider Tests

**File:** `tests/unit/test_fig_completion.cpp`
**Binary:** `test_fig_completion` (new)

| Test | What It Verifies |
|------|-----------------|
| LoadValidJson | Parses a valid Fig spec JSON correctly |
| ExtractSubcommands | Top-level subcommands extracted with names + descriptions |
| ExtractSubcommandAliases | `name: ["checkout", "co"]` both registered |
| ExtractOptions | Options at command level extracted |
| ExtractOptionAliases | `name: ["-b", "--branch"]` both registered |
| ExtractOptionDescription | Description field preserved |
| ExtractOptionArgs | Options with args have arg metadata |
| TraverseSubcommandTree | `git checkout -<TAB>` finds options under checkout |
| TraverseNestedSubcommands | `docker compose up -<TAB>` traverses 3 levels |
| MissingFieldsNoError | Spec with missing optional fields loads without crash |
| InvalidJsonNoError | Malformed JSON -> skip file, log warning |
| EmptySpecNoError | Valid JSON but empty spec -> no completions |
| PriorityOverFish | Fig completion overrides Fish for same command |

### 26.4 Smart History Tests

**File:** `tests/unit/test_sqlite_history.cpp`
**Binary:** `test_sqlite_history` (new, links SQLite3 + shell_lib + GTest)
**Fixture:** `SqliteHistoryFixture` -- creates temp db, cleans up after

| Test | What It Verifies |
|------|-----------------|
| RecordAndRetrieve | Record entry, search by command text, find it |
| RecordAllFields | timestamp, directory, exit_code, duration, hostname, session_id all stored |
| SearchByText | `search("git")` finds `git status`, `git push` |
| SearchFuzzy | `search("gtp")` matches `git push` via LIKE |
| FilterByDirectory | SearchFilter{directory="/project"} only returns commands from /project |
| FilterByExitCode | SearchFilter{exit_code=1} only returns failures |
| FilterBySince | SearchFilter{since=timestamp} only returns recent commands |
| FilterCombined | Multiple filters AND together |
| RecentReturnsOrdered | recent(5) returns last 5 commands by timestamp DESC |
| DedupConsecutive | Same command twice in a row -> only stored once |
| DedupAllowsNonConsecutive | Same command with different command between -> both stored |
| IgnoreLeadingSpace | " secret command" not recorded (privacy) |
| MigrationFromPlainText | Existing ~/.tash_history imported correctly |
| MigrationPreservesOrder | Imported commands maintain their order |
| MigrationBacksUpOldFile | Old file renamed to .bak |
| LargeHistory | 100,000 entries: insert + search completes in <1s |
| ConcurrentAccess | Two sessions writing simultaneously don't corrupt db |
| HistoryBuiltinHere | `history --here` filters by cwd |
| HistoryBuiltinFailed | `history --failed` filters by non-zero exit |
| HistoryBuiltinStats | `history stats` shows command frequency |

**File:** `tests/integration/test_smart_history.cpp`

| Test | What It Verifies |
|------|-----------------|
| CommandRecordedToSqlite | Run a command, check it exists in db |
| ExitCodeRecorded | Failed command has correct exit_code |
| DurationRecorded | Command with sleep has non-zero duration |
| HistoryCommandShowsTimestamp | `history` output includes time information |
| HistoryHereFilters | `history --here` in /tmp only shows /tmp commands |

### 26.5 Atuin Bridge Tests

**File:** `tests/unit/test_atuin_hook.cpp`

| Test | What It Verifies |
|------|-----------------|
| DetectsAtuinInPath | Returns true when mock atuin exists |
| DetectsAtuinMissing | Returns false when atuin not in PATH |
| BeforeCommandCallsStart | on_before_command invokes `atuin history start` |
| AfterCommandCallsEnd | on_after_command invokes `atuin history end` with correct exit/duration |
| AfterCommandRunsAsync | on_after_command doesn't block (fire-and-forget) |

### 26.6 AI Error Recovery Tests

**File:** `tests/unit/test_ai_error_hook.cpp`
**Fixture:** `AiErrorFixture` -- mock LLM client that returns canned responses

| Test | What It Verifies |
|------|-----------------|
| TriggersOnNonZeroExit | exit_code=1 with stderr -> AI called |
| SkipsOnExitZero | exit_code=0 -> AI not called |
| SkipsOnCtrlC | exit_code=130 -> AI not called |
| SkipsOnCommandNotFound | exit_code=127 -> AI not called (handled by "did you mean?") |
| SkipsOnEmptyStderr | Non-zero exit but empty stderr -> AI not called |
| SkipsWhenAiDisabled | state.ai_enabled=false -> AI not called |
| RateLimiterBlocks | Two failures within 5s -> second one skipped |
| RateLimiterAllowsAfterCooldown | Failure after 5s -> AI called |
| ParsesJsonResponse | `{"explanation":"...", "fix":"..."}` parsed correctly |
| ParsesExplanationOnly | `{"explanation":"...", "fix":""}` -> no fix offered |
| HandlesMalformedResponse | Non-JSON response -> falls back to raw text display |
| ContextIncludesCommand | Sent JSON includes the failed command |
| ContextIncludesStderr | Sent JSON includes stderr output |
| ContextIncludesDirectory | Sent JSON includes cwd |
| ContextIncludesRecentCommands | Sent JSON includes last 3 commands |

**File:** `tests/integration/test_ai_error_recovery.cpp`

| Test | What It Verifies |
|------|-----------------|
| FailedCommandShowsExplanation | `false_command` shows AI explanation inline |
| AiErrorsOffDisables | After `@ai errors off`, failures don't trigger AI |
| AiErrorsOnReenables | After `@ai errors on`, failures trigger AI again |

### 26.7 Theme Tests

**File:** `tests/unit/test_theme.cpp`
**Binary:** `test_theme` (new)

| Test | What It Verifies |
|------|-----------------|
| ParseHexColor | "#a6e3a1" -> RGB{166, 227, 161} |
| ParseHexUppercase | "#A6E3A1" -> same result |
| ParseInvalidHex | "not-a-color" -> fallback to default |
| ParseShortHex | "#fff" -> RGB{255, 255, 255} |
| LoadValidToml | Full theme file loads all fields |
| LoadMissingFieldUsesDefault | Missing `[completion]` section -> uses Catppuccin Mocha defaults |
| LoadMissingFileReturnsFallback | Non-existent theme file -> returns Catppuccin Mocha |
| BundledThemesMochaValid | catppuccin-mocha.toml loads without errors |
| BundledThemesLatteValid | catppuccin-latte.toml loads without errors |
| BundledThemesTokyoNightValid | tokyo-night.toml loads without errors |
| BundledThemesDraculaValid | dracula.toml loads without errors |
| BundledThemesNordValid | nord.toml loads without errors |
| ThemeListFindsAll | `theme list` finds all 5 bundled themes |
| ThemeSetPersistsToConfig | `theme set dracula` writes to config.toml |
| LightDarkAutoDetectDark | OSC 11 response with low luminance -> selects dark theme |
| LightDarkAutoDetectLight | OSC 11 response with high luminance -> selects light theme |
| LightDarkTimeoutDefaultsDark | No OSC 11 response -> defaults to dark |

**File:** `tests/integration/test_themes.cpp`

| Test | What It Verifies |
|------|-----------------|
| ThemeSetChangesColors | `theme set` changes prompt/highlight colors |
| ThemeListOutput | `theme list` shows all available themes |
| CustomThemeLoads | User-created theme in ~/.tash/themes/ loads |

### 26.8 Starship Tests

**File:** `tests/unit/test_starship.cpp`

| Test | What It Verifies |
|------|-----------------|
| DetectsStarshipInPath | Returns true when starship exists |
| DetectsStarshipMissing | Returns false when not in PATH |
| RendersWithCorrectArgs | Calls starship prompt with --status, --cmd-duration, --jobs |
| PassesExitStatus | Last exit code forwarded correctly |
| PassesDuration | Command duration converted to ms correctly |
| PassesJobCount | Background process count forwarded |
| SetsShellEnvVar | STARSHIP_SHELL=bash set before call |

### 26.9 Fuzzy Finder Tests

**File:** `tests/unit/test_fuzzy_finder.cpp`
**Binary:** `test_fuzzy_finder` (new)

| Test | What It Verifies |
|------|-----------------|
| ExactMatchHighestScore | "git" scores highest against "git" |
| PrefixMatchScoresHigh | "gi" scores well against "git" |
| BoundaryMatchScoresHigh | "gco" scores well against "git checkout" |
| SubsequenceMatches | "gchk" matches "git checkout" |
| NoMatchReturnsZero | "xyz" scores 0 against "git" |
| CaseSensitive | "Git" scores lower than "git" against "git" |
| ShorterCandidatePreferred | Equal match quality -> shorter candidate wins |
| RankingCorrect | Top result for "gco" is "git checkout" not "gcc -o" |
| EmptyQueryMatchesAll | "" matches everything with equal score |
| SpecialCharacters | Query with spaces/dots handled |
| LargeCandidateSet | 10,000 candidates: scoring completes in <50ms |
| GitBranchListParsed | `git branch` output parsed into candidates |
| GitLogParsed | `git log --oneline` output parsed into candidates |
| FileListRespectsGitignore | Files in .gitignore excluded from Ctrl+T results |

### 26.10 Command Safety Tests

**File:** `tests/unit/test_safety_hook.cpp`

| Test | What It Verifies |
|------|-----------------|
| DetectsRmRfSlash | `rm -rf /` flagged as CRITICAL |
| DetectsRmRfWildcard | `rm -rf /*` flagged as CRITICAL |
| DetectsRmRfPath | `rm -rf /important` flagged as HIGH |
| DetectsRmRecursive | `rm -r dir/` flagged as MEDIUM |
| DetectsChmodRecursive777 | `chmod -R 777 /` flagged as HIGH |
| DetectsGitForceePush | `git push --force` flagged as HIGH |
| DetectsGitResetHard | `git reset --hard` flagged as HIGH |
| DetectsDd | `dd if=/dev/zero of=/dev/sda` flagged as HIGH |
| DetectsMkfs | `mkfs.ext4 /dev/sda1` flagged as CRITICAL |
| DetectsTruncation | `> existing_file` flagged as MEDIUM |
| AllowsSafeCommands | `ls`, `echo`, `cat` -> no warning |
| AllowsRmSingleFile | `rm file.txt` -> no warning (not recursive) |
| BackslashEscapeBypassesHook | `\rm -rf dir/` bypasses safety check |
| TrashMvCreatesTrashDir | trash_rm=true: rm creates ~/.tash/trash/ |
| TrashMvPreservesFile | File exists in trash after rm |
| TrashManifestRecordsPath | Original path written to .manifest |
| TrashRestoreWorks | `trash restore` puts file back |
| TrashListShowsItems | `trash list` shows trashed items with timestamps |
| TrashEmptyDeletesAll | `trash empty` removes all trash contents |
| TrashEmptyOlderThan | `trash empty --older 30d` only deletes old items |
| DryRunSuggested | rsync --delete suggests --dry-run |
| DryRunNotSuggestedForUnknown | Unknown command -> no dry-run suggestion |

**File:** `tests/integration/test_safety.cpp`

| Test | What It Verifies |
|------|-----------------|
| RmRfShowsWarningInShell | Interactive rm -rf prompts user |
| TrashRmMovesFile | rm with trash_rm creates file in trash dir |
| TrashRestoreIntegration | Full trash -> restore cycle works |

### 26.11 Contextual AI Tests

**File:** `tests/unit/test_contextual_ai.cpp`
**Fixture:** `ContextualAiFixture` -- mock LLM

| Test | What It Verifies |
|------|-----------------|
| DetectsQuestionSuffix | "find large files?" detected as AI query |
| IgnoresValidCommandWithQuestion | `test -f file?` NOT treated as AI query (valid command) |
| IgnoresTrailingQuestionInString | `echo "what?"` NOT treated as AI query |
| ContextIncludesCwd | Sent prompt includes current directory |
| ContextIncludesRecentCommands | Last 5 commands included |
| ContextIncludesGitBranch | Current branch included when in git repo |
| ContextIncludesProjectType | CMakeLists.txt detected -> "C++ project" |
| ContextDetectsNodeProject | package.json -> "Node.js project" |
| ContextDetectsRustProject | Cargo.toml -> "Rust project" |
| ContextDetectsPythonProject | requirements.txt/pyproject.toml -> "Python project" |
| AiCompletionCached | Same prefix returns cached result without new LLM call |
| AiCompletionRateLimited | Rapid typing doesn't spam LLM |
| AiCompletionDelayConfigurable | completion_delay setting respected |

### 26.12 Pipeline Builder Tests

**File:** `tests/unit/test_pipeline.cpp`
**Binary:** `test_pipeline` (new)

| Test | What It Verifies |
|------|-----------------|
| ParsePipeOperator | `cmd1 \|> cmd2` parsed as structured pipe |
| TraditionalPipeUnchanged | `cmd1 \| cmd2` still works as text pipe |
| MixedPipes | `cmd1 \| cmd2 \|> where x > 1` works |
| AutoDetectJson | `[{"a":1}]` detected as JSON |
| AutoDetectText | Plain text converted to array of line objects |
| WhereFilterNumeric | `where size > 100` filters correctly |
| WhereFilterString | `where name == "test"` filters correctly |
| SortByColumn | `sort-by size` sorts ascending |
| SortByColumnDesc | `sort-by size --desc` sorts descending |
| SelectColumns | `select name size` keeps only specified columns |
| RejectColumns | `reject permissions` removes specified column |
| FirstN | `first 5` returns top 5 rows |
| LastN | `last 3` returns bottom 3 rows |
| CountRows | `count` returns row count |
| UniqDeduplicates | `uniq` removes duplicate rows |
| GroupByColumn | `group-by type` groups correctly |
| ToJson | `to-json` outputs valid JSON |
| ToCsv | `to-csv` outputs valid CSV with headers |
| ToTable | `to-table` outputs Unicode box-drawing table |
| TableFallbackAscii | Non-unicode terminal gets ASCII table |
| ChainedOperators | `where x > 1 \|> sort-by x \|> first 3` chains correctly |
| EmptyInput | Empty data through operators -> no crash |
| LsWrapper | `ls \|>` produces structured output with name, size, type |
| PsWrapper | `ps \|>` produces structured output with pid, cpu, mem |
| JsonFromCurl | `curl ... \|> where active == true` parses API response |

**File:** `tests/integration/test_pipeline.cpp`

| Test | What It Verifies |
|------|-----------------|
| StructuredPipeEndToEnd | `ls \|> where type == "file" \|> count` returns number |
| TraditionalPipeStillWorks | `ls \| grep .cpp \| wc -l` works unchanged |
| TableRendered | `ps \|> first 3 \|> to-table` shows box-drawing output |

### 26.13 Block Output Tests

**File:** `tests/unit/test_block_renderer.cpp`

| Test | What It Verifies |
|------|-----------------|
| BlockHeaderFormat | Header contains command, duration, status icon |
| SuccessBlockGreenIcon | exit_code=0 shows green checkmark |
| FailedBlockRedIcon | exit_code!=0 shows red X |
| BlockSeparatorDrawn | Blocks separated by horizontal line |
| FoldReducesOutput | Folded block shows header only |
| UnfoldRestoresOutput | Unfolded block shows full output |
| AutoFoldLongOutput | Output > block_fold_long lines auto-folded |
| DisabledByDefault | blocks=false -> normal output rendering |

### 26.14 Smart Alias Tests

**File:** `tests/unit/test_alias_suggest.cpp`

| Test | What It Verifies |
|------|-----------------|
| YouShouldUseExactMatch | `git checkout` with alias `gco='git checkout'` -> reminder |
| YouShouldUsePrefixMatch | `git checkout main` with alias `gco='git checkout'` -> reminder |
| NoReminderForUnaliased | Command without alias -> no reminder |
| OncePerSession | Same alias reminder shown max once per session |
| AliasSuggestFromHistory | Frequent `git status` in history -> suggests `alias gst='git status'` |
| AliasSuggestSkipsExisting | Already aliased commands not suggested |
| AliasPackListShowsPacks | `alias pack list` shows git, docker, kubectl, npm |
| AliasPackEnableAddsAliases | `alias pack enable git` adds all git aliases |
| AliasPackDisableRemoves | `alias pack disable git` removes pack aliases |

### 26.15 Inline Documentation Tests

**File:** `tests/unit/test_inline_docs.cpp`

| Test | What It Verifies |
|------|-----------------|
| HintFromFigSpec | `tar` shows Fig description as hint |
| HintFromFishCompletion | Unknown-to-Fig command shows Fish description |
| HintFallbackToTldr | No Fig/Fish -> tries tldr summary |
| HintFallbackToNothing | No source available -> no hint shown |
| ExplainBreaksDownFlags | `explain tar -xzf` shows -x, -z, -f descriptions |
| ExplainUnknownFlagFallback | Unknown flag -> shows "unknown" or defers to AI |
| FlagDescriptionsInCompletion | Tab completion shows description alongside flag |

### 26.16 Rich Output Tests

**File:** `tests/unit/test_rich_output.cpp`

| Test | What It Verifies |
|------|-----------------|
| DetectsHttpUrl | "https://example.com" detected |
| DetectsHttpsUrl | "https://example.com/path?q=1" detected |
| IgnoresNonUrl | "not a url" not detected |
| IgnoresPartialUrl | "http://" alone not detected |
| Osc8WrapCorrect | URL wrapped with correct OSC 8 escape sequences |
| DisabledWhenPiped | No OSC 8 wrapping when stdout is a pipe |
| TableHeuristicDetectsHeader | "PID CPU MEM COMMAND" detected as table header |
| TableHeuristicColumnsAligned | Consistent column positions detected |
| TableHeuristicRejectsMismatch | Unaligned text not detected as table |
| TableRenderBoxDrawing | Detected table rendered with Unicode borders |
| TableDisabledWhenPiped | No table enhancement when piped |
| ImageDetectsKittySupport | Kitty protocol query response parsed |
| ImageDetectsSixelSupport | Sixel support detected |
| ImageFallbackShowsInfo | No graphics support -> shows file dimensions/size |

### 26.17 Clipboard Tests

**File:** `tests/unit/test_clipboard.cpp`

| Test | What It Verifies |
|------|-----------------|
| Osc52EncodeCorrect | Text encoded to correct OSC 52 escape sequence |
| Osc52Base64Correct | Base64 encoding matches expected output |
| FallbackToMacOs | OSC 52 unsupported + macOS -> uses pbcopy |
| FallbackToLinux | OSC 52 unsupported + Linux -> tries xclip/xsel/wl-copy |
| CopyBuiltinPipe | `echo hello \| copy` sets clipboard |
| CopyBuiltinLiteral | `copy "text"` sets clipboard |
| PasteProtectionMultiline | 5-line paste detected and prompted |
| PasteProtectionSingleLine | 1-line paste executed normally |

### 26.18 Man Page Completion Tests

**File:** `tests/unit/test_manpage_completion.cpp`

| Test | What It Verifies |
|------|-----------------|
| ParseShortOption | `-f` extracted from man page |
| ParseLongOption | `--file` extracted from man page |
| ParseOptionWithDescription | `-f, --file FILE  Read from file` parsed |
| ParseHelpOutput | `--help` output regex extraction works |
| HelpTimeoutHandled | Command that hangs -> 2s timeout, no crash |
| CacheMissTriggersGeneration | Unknown command + no cache -> generates |
| CacheHitReturnsQuickly | Cached command completes in <10ms |
| PriorityBelowFishAndFig | Man page completions only used when Fish/Fig don't cover |
| CompletionStatusCount | `tash completions status` shows correct counts |

### 26.19 Config Sync Tests

**File:** `tests/unit/test_config_sync.cpp`

| Test | What It Verifies |
|------|-----------------|
| SyncInitCreatesGitRepo | `tash sync init` creates .git in ~/.tash/ |
| SyncRemoteSetsOrigin | `tash sync remote <url>` configures git remote |
| SyncPushCommitsAndPushes | `tash sync push` creates commit and pushes |
| SyncPullUpdatesConfig | `tash sync pull` updates local config |
| SyncExcludesHistoryDb | history.db not included in sync |
| SyncExcludesTrash | trash/ directory not included |
| SyncExcludesSessions | sessions/ directory not included |
| SyncDiffShowsChanges | `tash sync diff` shows pending changes |
| LinkTashrcCreatesSymlink | `tash sync link-tashrc` creates correct symlink |

### 26.20 Session Persistence Tests

**File:** `tests/unit/test_session.cpp`

| Test | What It Verifies |
|------|-----------------|
| SessionCreateSocket | `--persist name` creates Unix socket |
| SessionAttachConnects | `--attach name` connects to socket |
| SessionListShowsActive | `--sessions` lists active sessions |
| SessionDetachKeepsDaemon | Socket close doesn't kill daemon |
| SessionReattachRestoresCwd | After reattach, cwd matches detach point |
| SessionReattachRestoresEnv | After reattach, exported vars restored |
| SessionReattachRestoresAliases | After reattach, aliases restored |
| SessionKillTerminatesDaemon | `--kill name` stops session |
| SessionAutoCleanup | Inactive session cleaned after timeout |
| SessionGcRemovesStale | `--sessions --gc` cleans dead sockets |

### 26.21 Startup Optimization Tests

**File:** `tests/unit/test_startup.cpp`

| Test | What It Verifies |
|------|-----------------|
| BenchmarkFlagProducesOutput | `tash --benchmark` shows timing breakdown |
| BenchmarkAllStagesMeasured | All stages (config, history, plugins, prompt) have timings |
| LazyCompletionNotLoadedAtStart | Fish files not parsed until first TAB |
| LazyHistoryNotQueriedAtStart | SQLite not queried until first history access |
| ConfigCacheFasterThanParse | Binary cache load < TOML parse time |
| ConfigCacheInvalidatedOnMtime | Changed config.toml triggers re-parse |
| ColdStartUnder100ms | Full startup completes in <100ms |
| WarmStartUnder50ms | Cached startup completes in <50ms |

### 26.22 Regression Tests

**File:** `tests/integration/test_regression.cpp`

| Test | What It Verifies |
|------|-----------------|
| AllExistingTestsPass | 275 existing tests still green |
| PipesStillWork | `echo hello \| cat` unchanged behavior |
| RedirectsStillWork | `echo hello > file` unchanged |
| AliasesStillWork | `alias ll='ls -la'` then `ll` works |
| AutoCdStillWorks | Typing directory name changes to it |
| ZJumpStillWorks | `z` frecency navigation unchanged |
| HistoryBangStillWorks | `!!` and `!n` still expand |
| AiCommandStillWorks | `@ai how do I...` still routes to AI |
| GlobsStillWork | `*.cpp` still expands |
| BackgroundJobsStillWork | `sleep 1 &` runs in background |
| CtrlDStillExits | Double Ctrl+D exits shell |
| TashrcStillSourced | ~/.tashrc commands executed on startup |
| NoSegfaultOnAnyFeature | Run all new features, check exit != 139 |

### 26.23 Test Infrastructure Changes

**CMakeLists.txt additions:**
```
# New unit test binaries
add_executable(test_plugin_registry tests/unit/test_plugin_registry.cpp)
add_executable(test_fish_completion tests/unit/test_fish_completion.cpp)
add_executable(test_fig_completion tests/unit/test_fig_completion.cpp)
add_executable(test_sqlite_history tests/unit/test_sqlite_history.cpp)
add_executable(test_theme tests/unit/test_theme.cpp)
add_executable(test_fuzzy_finder tests/unit/test_fuzzy_finder.cpp)
add_executable(test_pipeline tests/unit/test_pipeline.cpp)

# New integration test files added to test_integration binary
# tests/integration/test_fish_completion.cpp
# tests/integration/test_smart_history.cpp
# tests/integration/test_ai_error_recovery.cpp
# tests/integration/test_themes.cpp
# tests/integration/test_safety.cpp
# tests/integration/test_pipeline.cpp
# tests/integration/test_regression.cpp
```

**Test data directory:** `tests/data/` containing:
- `fish_completions/` -- sample Fish completion files for parsing tests
- `fig_specs/` -- sample Fig JSON specs for loading tests
- `themes/` -- sample TOML theme files for theme tests
- `manpages/` -- sample man page files for parsing tests
- `alias_packs/` -- sample alias pack TOML files

**Mock classes:**
- `MockLLMClient` -- returns canned responses, tracks call count
- `MockCompletionProvider` -- returns configurable completions
- `MockHistoryProvider` -- in-memory history for testing
- `MockHookProvider` -- records calls for verification

### 26.24 Test Count Estimate

| Category | Existing | New | Total |
|----------|---------|-----|-------|
| Unit Tests | ~130 | ~270 | ~400 |
| Integration Tests | ~145 | ~45 | ~190 |
| **Total** | **~275** | **~315** | **~590** |

---

## 27. Branch Plan

### Phase 1: Foundation (merge first)

| Branch | Features | Est. LOC | Est. Tests |
|--------|----------|----------|-----------|
| feature/plugin-core | Interfaces, registry, config system, TOML | ~400 | ~13 |

### Phase 2: Core Features (all independent, merge after core)

| Branch | Features | Est. LOC | Est. Tests |
|--------|----------|----------|-----------|
| feature/fish-completions | Fish + Fig completion providers | ~800 | ~36 |
| feature/smart-history | SQLite history, Atuin bridge | ~480 | ~29 |
| feature/ai-error-recovery | AI error hook provider | ~200 | ~18 |
| feature/configurable-themes | Theme system, 5 themes, light/dark detect | ~400 | ~20 |
| feature/starship-support | Starship prompt provider | ~50 | ~7 |

### Phase 3: Advanced Features (all independent, merge after core)

| Branch | Features | Est. LOC | Est. Tests |
|--------|----------|----------|-----------|
| feature/fuzzy-finder | Ctrl+R/T/G/P fuzzy interfaces | ~500 | ~14 |
| feature/command-safety | Destructive warnings, trash rm, dry-run | ~250 | ~25 |
| feature/contextual-ai | ? suffix, AI completions, context accumulation | ~350 | ~13 |
| feature/pipeline-builder | \|> operator, structured operators, table render | ~600 | ~28 |
| feature/block-output | Warp-style block rendering, fold/nav | ~400 | ~8 |
| feature/smart-aliases | "You should use", AI suggest, alias packs | ~200 | ~9 |
| feature/inline-docs | Hint text, explain builtin, flag descriptions | ~300 | ~7 |
| feature/rich-output | Clickable URLs, inline images, auto-tables, output sharing | ~450 | ~14 |
| feature/session-persistence | Persistent sessions via Unix sockets | ~400 | ~10 |
| feature/startup-optimization | Benchmarking, lazy loading, CI perf gate | ~150 | ~8 |
| feature/clipboard | OSC 52, copy/paste builtins, paste protection | ~150 | ~8 |
| feature/manpage-completions | Man page parser, --help parser, cache | ~450 | ~9 |
| feature/config-sync | Git-based config sync | ~300 | ~9 |

**Total estimated: ~6,830 LOC new code + ~315 new tests = ~590 total tests**

plugin-core merges first. All Phase 2 and Phase 3 branches are independent of each other and can merge in any order after core.

---

## 28. Success Criteria

### Functional
- tash starts in <50ms warm / <100ms cold with all plugins loaded
- git TAB shows subcommands with descriptions from Fish/Fig/manpages
- grep --col TAB completes to --color with description
- `tash completions status` reports 2,000+ commands covered
- Failed commands show AI explanation + fix suggestion
- history --here filters by current directory
- theme set tokyo-night changes all colors immediately
- Auto light/dark theme detection works in supported terminals
- prompt set starship switches to Starship rendering
- Ctrl+T opens fuzzy file finder, Ctrl+G shows git branches
- `rm -rf important/` shows safety warning before executing
- `trash list` / `trash restore` work correctly
- `compress all logs?` generates the right find+tar command
- `ps |> where cpu > 1.0 |> sort-by cpu` renders a filtered table
- Command output shows in collapsible blocks with duration
- Typing `git checkout` when alias `gco` exists shows reminder
- `explain tar -xzf archive.tar.gz` breaks down each flag
- URLs in output are clickable in supported terminals
- `echo text | copy` puts text on clipboard via OSC 52
- Multiline paste shows protection prompt
- `tash --persist` creates recoverable session
- `tash sync push` / `tash sync pull` sync config across machines
- `tash export last --gist` creates a GitHub gist

### Quality
- All existing 275 tests continue to pass
- ~315 new tests pass (total ~590)
- No functionality regression when ecosystem tools are not installed
- No segfaults on any feature (AddressSanitizer clean)
- Startup time tracked in CI, fails on regression beyond 150ms
- Code coverage >= 80% on new code
