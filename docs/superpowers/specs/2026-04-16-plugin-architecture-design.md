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
provider = "gemini"    # "gemini", "openai", "ollama"
```

Created with sensible defaults on first run.

---

## 10. Build System Changes

### CMakeLists.txt Additions

- SQLite3: find_package, conditional compile with TASH_SQLITE_ENABLED
- toml11: FetchContent from GitHub (header-only, v4.2.0)
- Plugin source files always compiled (fish, fig, theme, starship, registry)
- SQLite-dependent sources conditional (sqlite_history, atuin_hook)
- AI-dependent sources conditional (ai_error_hook)

### Graceful Degradation

| Dependency | Missing | Behavior |
|-----------|---------|----------|
| SQLite3 | Not found | Fall back to plain-text history |
| Fish completions dir | Not found | Skip, use built-in only |
| Fig JSON cache | Not found | Skip Fig completions |
| Starship binary | Not found | Use built-in prompt |
| Atuin binary | Not found | Skip Atuin bridge |
| OpenSSL (AI) | Not found | Skip AI error recovery |

Tash always works. Ecosystem integrations enhance when available.

---

## 11. Testing Strategy

### Unit Tests (per feature)

- Plugin Registry: register/dispatch, priority ordering, multiple providers
- Fish Parser: parse complete lines, handle quotes, flags, malformed input
- Fig Loader: load JSON spec, traverse subcommands, option matching
- SQLite History: record/search/filter, migration, dedup
- AI Error Hook: trigger conditions, rate limiting, JSON response parsing
- Theme Parser: load TOML, hex parsing, fallback on missing fields
- Starship Provider: argument construction, env var setup

### Integration Tests

- git TAB with Fish completions -> returns subcommands with descriptions
- grep --TAB with Fig specs -> returns long options
- Command fails -> AI suggests fix
- history --here -> only shows commands from current directory
- theme set dracula -> colors change immediately
- Starship prompt rendering -> contains Starship output

---

## 12. Branch Plan

| Branch | Features | Merge Order |
|--------|----------|-------------|
| feature/plugin-core | Interfaces, registry, config, TOML | First (foundation) |
| feature/fish-completions | Fish + Fig completion providers | After core |
| feature/smart-history | SQLite history, Atuin bridge | After core |
| feature/ai-error-recovery | AI error hook | After core |
| feature/configurable-themes | Theme system, 5 themes | After core |
| feature/starship-support | Starship prompt provider | After core |

plugin-core merges first, then all others can merge independently in any order.

---

## 13. Success Criteria

- tash starts in <100ms with all plugins loaded
- git TAB shows subcommands with descriptions from Fish/Fig
- grep --col TAB completes to --color with description
- Failed commands show AI explanation + fix suggestion
- history --here filters by current directory
- theme set tokyo-night changes all colors immediately
- prompt set starship switches to Starship rendering
- All existing tests continue to pass
- No functionality regression when ecosystem tools are not installed
