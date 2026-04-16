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

## 20. Build System Changes

### CMakeLists.txt Additions

- SQLite3: find_package, conditional compile with TASH_SQLITE_ENABLED
- toml11: FetchContent from GitHub (header-only, v4.2.0)
- Plugin source files always compiled (fish, fig, theme, starship, registry)
- SQLite-dependent sources conditional (sqlite_history, atuin_hook, alias_suggest)
- AI-dependent sources conditional (ai_error_hook, contextual_ai)

### Graceful Degradation

| Dependency | Missing | Behavior |
|-----------|---------|----------|
| SQLite3 | Not found | Fall back to plain-text history, no alias suggest |
| Fish completions dir | Not found | Skip, use built-in only |
| Fig JSON cache | Not found | Skip Fig completions |
| Starship binary | Not found | Use built-in prompt |
| Atuin binary | Not found | Skip Atuin bridge |
| OpenSSL (AI) | Not found | Skip AI error recovery, contextual AI, explain builtin AI fallback |
| tldr binary | Not found | Skip tldr hints, use Fish/Fig descriptions only |

Tash always works. Ecosystem integrations enhance when available.

---

## 21. Testing Strategy

### Unit Tests (per feature)

- Plugin Registry: register/dispatch, priority ordering, multiple providers
- Fish Parser: parse complete lines, handle quotes, flags, malformed input
- Fig Loader: load JSON spec, traverse subcommands, option matching
- SQLite History: record/search/filter, migration, dedup
- AI Error Hook: trigger conditions, rate limiting, JSON response parsing
- Theme Parser: load TOML, hex parsing, fallback on missing fields
- Starship Provider: argument construction, env var setup
- Fuzzy Finder: scoring algorithm, boundary matching, ranking
- Safety Hook: pattern detection, trash mv/restore, manifest tracking
- Contextual AI: ? suffix detection, valid-command vs natural-language heuristic
- Structured Pipe: |> parsing, JSON auto-detection, operator execution, table rendering
- Block Renderer: block boundary tracking, fold/unfold state
- Alias Suggest: frequency counting, pack loading, "you-should-use" matching
- Inline Docs: hint text source priority, explain flag breakdown
- Rich Output: URL regex, OSC 8 wrapping, table heuristic detection
- Session: socket lifecycle, state serialization/deserialization
- Startup Benchmark: timing accuracy, lazy-load verification

### Integration Tests

- git TAB with Fish completions -> returns subcommands with descriptions
- grep --TAB with Fig specs -> returns long options
- Command fails -> AI suggests fix
- history --here -> only shows commands from current directory
- theme set dracula -> colors change immediately
- Starship prompt rendering -> contains Starship output
- Ctrl+T opens file finder, selecting inserts path
- Ctrl+G shows git branches, selecting inserts branch name
- rm -rf with safety hook -> warning prompt appears
- `find large files?` -> AI generates find command
- `ps |> where cpu > 1.0` -> filtered table output
- Block output shows command header with duration and status
- Typing aliased command -> "you should use" reminder appears
- `explain tar -xzf` -> flag breakdown displayed
- URLs in output are clickable (OSC 8)
- `tash --persist` + `tash --attach` -> session restored

---

## 22. Branch Plan

### Phase 1: Foundation (merge first)

| Branch | Features | Est. LOC |
|--------|----------|----------|
| feature/plugin-core | Interfaces, registry, config system, TOML | ~400 |

### Phase 2: Core Features (all independent, merge after core)

| Branch | Features | Est. LOC |
|--------|----------|----------|
| feature/fish-completions | Fish + Fig completion providers | ~800 |
| feature/smart-history | SQLite history, Atuin bridge | ~480 |
| feature/ai-error-recovery | AI error hook provider | ~200 |
| feature/configurable-themes | Theme system, 5 bundled themes | ~350 |
| feature/starship-support | Starship prompt provider | ~50 |

### Phase 3: Advanced Features (all independent, merge after core)

| Branch | Features | Est. LOC |
|--------|----------|----------|
| feature/fuzzy-finder | Ctrl+R/T/G/P fuzzy interfaces | ~500 |
| feature/command-safety | Destructive warnings, trash rm, dry-run | ~250 |
| feature/contextual-ai | ? suffix, AI completions, context accumulation | ~350 |
| feature/pipeline-builder | |> operator, structured operators, table render | ~600 |
| feature/block-output | Warp-style block rendering, fold/nav | ~400 |
| feature/smart-aliases | "You should use", AI suggest, alias packs | ~200 |
| feature/inline-docs | Hint text, explain builtin, flag descriptions | ~300 |
| feature/rich-output | Clickable URLs, inline images, auto-tables | ~350 |
| feature/session-persistence | Persistent sessions via Unix sockets | ~400 |
| feature/startup-optimization | Benchmarking, lazy loading, CI perf gate | ~150 |

**Total estimated new code: ~5,780 LOC**

plugin-core merges first. All Phase 2 and Phase 3 branches are independent of each other and can merge in any order after core.

---

## 23. Success Criteria

- tash starts in <50ms warm / <100ms cold with all plugins loaded
- git TAB shows subcommands with descriptions from Fish/Fig
- grep --col TAB completes to --color with description
- Failed commands show AI explanation + fix suggestion
- history --here filters by current directory
- theme set tokyo-night changes all colors immediately
- prompt set starship switches to Starship rendering
- Ctrl+T opens fuzzy file finder, Ctrl+G shows git branches
- `rm -rf important/` shows safety warning before executing
- `compress all logs?` generates the right find+tar command
- `ps |> where cpu > 1.0 |> sort-by cpu` renders a filtered table
- Command output shows in collapsible blocks with duration
- Typing `git checkout` when alias `gco` exists shows reminder
- `explain tar -xzf archive.tar.gz` breaks down each flag
- URLs in output are clickable in supported terminals
- `tash --persist` creates recoverable session
- All existing 275 tests continue to pass
- No functionality regression when ecosystem tools are not installed
- Startup time tracked in CI, fails on regression beyond 150ms
