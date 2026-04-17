# Tash — A Modern Unix Shell

A feature-rich Unix shell written in C++ with syntax highlighting, autosuggestions, smart completions, and a Catppuccin color palette. Built as a deep exploration of systems programming — from `fork`/`exec` to signal handling to interactive line editing.

[![GitHub stars](https://img.shields.io/github/stars/tavakkoliamirmohammad/tash-shell?style=social)](https://github.com/tavakkoliamirmohammad/tash-shell/stargazers)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build](https://github.com/tavakkoliamirmohammad/tash-shell/actions/workflows/build.yml/badge.svg)](https://github.com/tavakkoliamirmohammad/tash-shell/actions)
[![Tests](https://img.shields.io/badge/tests-779%20passing-brightgreen)](https://github.com/tavakkoliamirmohammad/tash-shell/actions)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/tavakkoliamirmohammad/tash-shell/pulls)

<!-- TODO: Replace with actual screenshot/GIF of tash in action -->
![tash demo](docs/screenshot.png)

## Highlights

- **Syntax highlighting** — commands glow green if valid, red if not, as you type
- **Autosuggestions** — gray ghost text from history, press `Right` to accept
- **"Did you mean?"** — typo `gti` suggests `git` via Damerau-Levenshtein distance
- **Smart completions** — Tab completes builtins, PATH commands, git/docker subcommands, `$VAR` names, plus **1,700+ commands** via Fish + Fig/Amazon Q completion providers and `--help` parsing as a fallback
- **Configurable themes** — 5 bundled palettes (Catppuccin Mocha/Latte, Tokyo Night, Dracula, Nord), switch live with `theme set <name>`
- **Plugin architecture** — completion, prompt, history, and hook providers with a priority-based registry (Starship prompt, SQLite history, AI error recovery, safety net, alias reminders…)
- **AI powered** — `@ai` generates commands, explains errors, writes scripts; `?` suffix routes natural-language questions; automatic AI error recovery on failed commands (Gemini, OpenAI, or Ollama)
- **Smart history** — SQLite-backed with context (cwd, exit code, duration, hostname); `history --here`, `history --failed`, `history stats`; Atuin bridge
- **Structured pipelines** — `|>` operator with `where`, `sort-by`, `select`, `to-table`, `to-json`, `to-csv` for JSON data
- **Block-style output + rich rendering** — collapsible command blocks, OSC 8 clickable URLs, auto-detected tables with Unicode box-drawing
- **Safety net** — destructive-command detection (`rm -rf /`, `git push --force`, `dd`, `mkfs`…) with confirm prompts
- **Sessions + config sync** — save/restore named sessions (cwd, aliases, env); git-based config sync across machines
- **POSIX coverage** — heredocs (`<<`, `<<-`, quoted), subshells `(cmd; cmd)`, per-segment pipeline redirections, `trap` for signals + EXIT
- **779 automated tests** — Google Test suite (24 unit + 41 integration files) plus libFuzzer parser harness with coverage gate

## Features

| Category | Features |
|----------|----------|
| **Interactive** | Syntax highlighting, autosuggestions (ghost text), `Tab` completion, `Right` to accept hint, `Alt+Right` to accept one word, `Alt+.` to insert last argument |
| **Completions** | Builtins, PATH, `$VAR`, **Fish** completions (1,056 commands), **Fig/Amazon Q** specs (715 commands), `--help` parsing fallback, `cd`/`pushd` dirs-only, `kill`/`bgkill`/`bgstop`/`bgstart` PID list |
| **Execution** | Pipes (`\|`), command substitution (`$(cmd)`), script execution (`tash script.sh`), **subshells** `(cmd; cmd)`, **structured `\|>`** pipelines for JSON |
| **Redirection** | stdout (`>`, `>>`), stdin (`<`), stderr (`2>`, `2>&1`), **heredocs** (`<<`, `<<-`, `<<'DELIM'`), per-segment pipeline redirections |
| **Operators** | `&&` (and), `\|\|` (or), `;` (sequential), `\|>` (structured) |
| **Variables** | `$VAR`, `${VAR}`, `$?` (exit status), `$$` (PID), `export`, `unset` |
| **Expansion** | Glob (`*`, `?`, `[...]`), tilde (`~`), command substitution (`$(...)`) |
| **Navigation** | `cd`, `cd -`, `pushd`/`popd`/`dirs`, **auto-cd** (type directory name), **`z`** (frecency jump) |
| **Job Control** | `bg`, `fg`, `bglist`, `bgkill`, `bgstop`, `bgstart`, POSIX **`trap`** (signals + EXIT) |
| **Aliases** | `alias ll='ls -la'`, `unalias`, expansion before execution, **smart reminders** when you forget to use one |
| **History** | **SQLite-backed** `~/.tash/history.db` with context (cwd, exit code, duration, hostname), `history --here`, `history --failed`, `history stats`, Atuin bridge, dedup, ignore-space, `!!`, `!n`, arrow navigation |
| **Multiline** | Auto-continue on unclosed quotes or trailing `\|`/`&&`, backslash continuation |
| **Prompt** | Two-line prompt with git branch, dirty/clean status (`+*?`), exit code indicator (green/red `❯`), command duration; **Starship** auto-integration; built-in block-style output with collapsible command blocks |
| **Themes** | 5 bundled palettes (Catppuccin Mocha/Latte, Tokyo Night, Dracula, Nord); `theme list`, `theme set <name>`, `theme preview`; TOML theme files |
| **Rich output** | OSC 8 **clickable URLs**, auto-detected tables rendered with Unicode box-drawing, markdown export |
| **AI** | `@ai <anything>` — multi-provider (Gemini/OpenAI/Ollama), streaming output, conversation memory, `@ai config`; **`?` suffix** routes natural-language queries (`find all python files?`); **automatic error recovery** — AI explains failed commands and suggests a one-keypress fix |
| **Clipboard** | `copy`/`paste` builtins via OSC 52, works over SSH and inside tmux; pbcopy/xclip/wl-copy fallbacks; multi-line paste confirmation |
| **Sessions** | `tash --persist <name>` / `--attach` / `--sessions` / `--kill` — save/restore cwd, aliases, and env vars |
| **Config** | `~/.tashrc` loaded on startup, XDG-aware paths (`~/.tash/`, `$XDG_CONFIG_HOME`), **git-based config sync** across machines (`tash sync init/remote/push/pull/diff`) |
| **Plugin system** | `ICompletionProvider`, `IPromptProvider`, `IHistoryProvider`, `IHookProvider` — priority-based registry dispatches to all registered providers |
| **Safety** | **Destructive-command detection** (`rm -rf /`, `git push --force`, `dd`, `mkfs`, `chmod -R 777`…), Ctrl+D protection (double-press), bracketed paste, SIGINT handling |
| **Tooling** | `tash --benchmark` startup profiler, `explain <cmd>` inline docs for 30+ commands, built-in fuzzy finder (no fzf dependency) |
| **Platform** | Linux (Ubuntu, Fedora, Alpine) and macOS (Intel + ARM) |

## Quick Start

```sh
# Build
cmake -B build && cmake --build build

# Run
./build/tash.out
```

### Prerequisites

- **OpenSSL** and **libcurl** (for AI features) — available on all macOS and Linux systems
- The build system fetches [replxx](https://github.com/AmokHuginnsson/replxx), [nlohmann/json](https://github.com/nlohmann/json), [cpp-httplib](https://github.com/yhirose/cpp-httplib), and [Google Test](https://github.com/google/googletest) automatically via CMake FetchContent

```sh
# Ubuntu/Debian
sudo apt install libssl-dev libcurl4-openssl-dev libsqlite3-dev

# Fedora/RHEL/Rocky/Alma
sudo dnf install openssl-devel libcurl-devel sqlite-devel

# Alpine
apk add openssl-dev curl-dev sqlite-dev

# Arch
sudo pacman -S openssl curl sqlite

# macOS — included with Xcode Command Line Tools
xcode-select --install
```

> `install.sh` runs the right `apt`/`dnf`/`apk`/`pacman`/`brew` command automatically when it falls back to a source build, so most users don't need to run any of the above by hand.

### Install System-Wide

Latest tagged release (stable):

```sh
curl -sSL https://raw.githubusercontent.com/tavakkoliamirmohammad/tash-shell/master/install.sh | bash
```

Rolling snapshot of the latest `master` build (bleeding edge):

```sh
curl -sSL https://raw.githubusercontent.com/tavakkoliamirmohammad/tash-shell/master/install.sh | TASH_USE_MASTER=1 bash
```

Prebuilt binaries are published for:

| Artifact | Runs on |
|---|---|
| `tash-linux-amd64` | Ubuntu 20.04+, Debian 11+, Fedora 30+, RHEL/Rocky/Alma 8+, Amazon Linux 2023, Arch (glibc ≥ 2.28) |
| `tash-linux-arm64` | Same distros on arm64 — Raspberry Pi 64-bit, AWS Graviton, ARM servers |
| `tash-macos-arm64` | Apple Silicon (M1/M2/M3/M4), macOS 14+ |
| `tash-macos-amd64` | Intel Macs, macOS 13+ |

Any other platform falls back to a source build (`install.sh` auto-installs the needed dev headers first).

Or download the binary directly from the [`master-latest` pre-release](https://github.com/tavakkoliamirmohammad/tash-shell/releases/tag/master-latest) — refreshed on every push to `master`.

Or with Homebrew (latest tagged release, or `--HEAD` for bleeding-edge master):
```sh
brew install --formula Formula/tash.rb         # stable
brew install --HEAD --formula Formula/tash.rb  # latest master
```

After install, verify which features got compiled in:
```sh
tash --version
# tash 2.0.0
# features: +ai +sqlite-history +fish-completion +fig-completion +manpage-completion +clipboard +themes +trap +heredocs +subshells
```
A `-ai` or `-sqlite-history` token means the dev headers weren't present at build time — `install.sh` will flag this and tell you what to install.

## Usage

```
╭─ amir in ~/projects/tash on  master [*?]
❯ ls | grep cpp | wc -l
       6

╭─ amir in ~/projects/tash on  master
❯ gti status
gti: No such file or directory
tash: did you mean 'git'?

╭─ amir in ~/projects/tash on  master
❯ echo "today is $(date +%A)"
today is Friday

╭─ amir in ~/projects/tash on  master took 3.2s
❯ /tmp                          # auto-cd: just type a directory
/private/tmp

╭─ amir in /private/tmp on  master
❯ z proj                        # frecency jump to most-visited match
/Users/amir/projects

╭─ amir in ~/projects on  master
❯ export NAME=world && echo $NAME
world
```

### AI Features

Tash includes AI features with support for **Google Gemini** (free), **OpenAI**, and **Ollama** (local). Just type `@ai` followed by anything in natural language.

```
╭─ amir in ~/projects on  master
❯ @ai find all files larger than 100MB
tash ai ─
find . -type f -size +100M

╭─ amir in ~/projects on  master
❯ gcc -o main main.c
main.c:1:10: fatal error: 'stdio.h' file not found

╭─ amir in ~/projects on  master [1]
❯ @ai explain this error
tash ai ─
The compiler can't find 'stdio.h'. Install the development headers:
  sudo apt install build-essential    # Linux
  xcode-select --install              # macOS

╭─ amir in ~/projects on  master
❯ @ai what does tar -xzvf archive.tar.gz
tash ai ─
  -x  extract files from archive
  -z  decompress through gzip
  -v  verbose — list files as they're extracted
  -f  use the specified archive file

╭─ amir in ~/projects on  master
❯ @ai write a script to backup my home directory
tash ai ─
  #!/bin/bash
  tar -czf /tmp/home_backup_$(date +%F).tar.gz ~/
  echo "Backup complete"

╭─ amir in ~/projects on  master
❯ @ai how do I set up SSH keys for GitHub
tash ai ─
  1. Generate key: ssh-keygen -t ed25519 -C "you@email.com"
  2. Start agent: eval "$(ssh-agent -s)"
  3. Add key: ssh-add ~/.ssh/id_ed25519
  4. Copy public key: cat ~/.ssh/id_ed25519.pub
  5. Go to GitHub → Settings → SSH Keys → New SSH Key → paste
```

| Command | Description |
|---------|-------------|
| `@ai <anything>` | Ask the AI anything — commands, explanations, scripts, guidance. It figures out what you need. |
| `@ai config` | Configure provider, model, API keys, and view status. |
| `@ai clear` | Clear conversation history. |
| `@ai on` / `@ai off` | Enable or disable AI features. |

#### Setting Up a Provider

Run `@ai config` to interactively choose your provider and set API keys:

**Gemini (free):**
1. Go to [aistudio.google.com/apikey](https://aistudio.google.com/apikey)
2. Sign in, click "Create API Key", copy it
3. Run `@ai config` → option 3 → paste key

**OpenAI:**
1. Go to [platform.openai.com/api-keys](https://platform.openai.com/api-keys)
2. Create an API key
3. Run `@ai config` → option 1 → type `openai` → option 3 → paste key

**Ollama (local, free):**
1. Install Ollama: [ollama.com](https://ollama.com)
2. Run `ollama serve` and `ollama pull qwen3.5:0.8b`
3. Run `@ai config` → option 1 → type `ollama`

#### Conversation Memory

The AI remembers context within a session — ask follow-up questions naturally:

```
❯ @ai find files larger than 100MB
❯ @ai now delete them
❯ @ai clear     # reset conversation when done
```

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Tab` | Complete command, file, git/docker subcommand, or `$VAR` |
| `Right` | Accept the gray autosuggestion (at end of line) |
| `Alt+Right` | Accept one word from the autosuggestion |
| `Alt+.` | Insert the last argument of the previous command |
| `Up/Down` | Navigate command history |
| `Ctrl+R` | Reverse search through history |
| `Ctrl+L` | Clear screen |
| `Ctrl+C` | Cancel current input / kill foreground process |
| `Ctrl+D` | Exit shell (press twice) |

## Built-in Commands

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory. `cd -` returns to previous. `cd ~` goes home. |
| `z <pattern>` | Jump to most-visited directory matching pattern (frecency). |
| `pwd` | Print current directory. |
| `exit` | Exit the shell. |
| `history` | Show command history. `!!` repeats last, `!n` repeats nth. |
| `export VAR=val` | Set environment variable. No args lists all. |
| `unset VAR` | Remove environment variable. |
| `alias name='cmd'` | Create alias. No args lists all. |
| `unalias name` | Remove alias. |
| `source file` | Execute file in current shell (`. file` also works). |
| `which cmd` | Find command location or identify builtins. |
| `type cmd` | Same as `which`. |
| `clear` | Clear screen (Ctrl+L also works). |
| `bg cmd` | Run command in background. |
| `fg [n]` | Bring background job to foreground. |
| `bglist` | List background jobs. |
| `bgkill n` | Kill background job n. |
| `bgstop n` | Stop background job n. |
| `bgstart n` | Resume background job n. |
| `pushd dir` | Push directory onto stack and cd. |
| `popd` | Pop directory from stack and cd. |
| `dirs` | Show directory stack. |
| `trap 'cmd' SIG...` | POSIX signal/EXIT handler. `trap` lists, `trap - SIG` resets, `trap '' SIG` ignores. Recognizes EXIT, HUP, INT, QUIT, TERM, USR1, USR2. |
| `theme list\|set <name>\|preview` | List, switch, or preview color themes. |
| `explain <cmd> [args]` | Explain a command and its flags (built-in docs for 30+ commands). |
| `copy [text]` / `paste` | Clipboard via OSC 52 (or `pbcopy`/`xclip`/`wl-copy` fallback). Pipe in or out. |
| `linkify` / `table` / `block` | Rich output helpers — hyperlink URLs, render tables, collapsible command blocks. |
| `session save\|list\|rm\|load <name>` | Persist and restore shell state (cwd, aliases, env). |
| `config [get\|set\|sync...]` | Manage config and git-based config sync across machines. |
| `@ai <question>` | AI-powered assistant — ask anything in natural language. |
| `<question>?` | Trailing `?` routes a natural-language query to the AI (e.g., `find all python files larger than 1MB?`). |

## Architecture

```
Input → Replxx (highlighting + hints + completion)
  → History Expansion (!! / !n)
  → @ai interception (→ LLM API if AI command)
  → Multiline Continuation (unclosed quotes, trailing |/&&)
  → Parse Operators (&&, ||, ;)
  → For each command:
      Expand Variables ($VAR, $?, $$)
      → Command Substitution $(...)
      → Parse Redirections (>, >>, <, 2>, 2>&1)
      → Tokenize → Strip Quotes → Expand Aliases
      → Expand Globs → Auto-cd check
      → Dispatch: Builtin table | Background | Pipeline | fork/exec
      → "Did you mean?" on exit code 127
```

### Source Files

The tree is organized into layers: `src/core/` for shell execution, `src/ui/` for interactive features, `src/plugins/` for provider implementations, `src/util/` for shared infrastructure, and `src/ai/` for LLM integration.

| Layer | File | Purpose |
|-------|------|---------|
| **Core** | `src/core/parser.cpp` | Tokenizer, variable/command expansion, redirection parsing (including heredocs) |
| | `src/core/executor.cpp` | Command-line execution, subshells, per-segment pipeline dispatch |
| | `src/core/process.cpp` | fork/exec, pipelines, background processes |
| | `src/core/builtins.cpp` | Dispatch table for all built-in commands |
| | `src/core/signals.cpp` | Signal handling + POSIX `trap` delivery |
| | `src/core/session.cpp` | Session save/load/attach |
| | `src/core/config_sync.cpp` | Git-based config sync (init/remote/push/pull/diff) |
| | `src/core/structured_pipe.cpp` | `\|>` structured-data pipeline operators |
| **Builtins** | `src/builtins/{nav,env,bg,history,ui,shell}.cpp` | Grouped builtin implementations |
| **UI** | `src/ui/completion.cpp` | Tab completion + plugin registry dispatch |
| | `src/ui/highlight.cpp` | Syntax highlighting + autosuggestion hints |
| | `src/ui/suggest.cpp` | "Did you mean?" via Damerau-Levenshtein distance |
| | `src/ui/prompt.cpp` | Two-line prompt with git status and command duration |
| | `src/ui/fuzzy_finder.cpp` | Built-in fuzzy matching engine |
| | `src/ui/block_renderer.cpp` | Collapsible block-style command output |
| | `src/ui/rich_output.cpp` | OSC 8 hyperlinks + Unicode table rendering |
| | `src/ui/clipboard.cpp` | OSC 52 clipboard copy/paste |
| | `src/ui/inline_docs.cpp` | `explain` builtin flag database |
| **Plugins** | `src/plugins/plugin_registry.cpp` | Provider interfaces + priority-based dispatch |
| | `src/plugins/fish_completion_provider.cpp` | Fish completion file parser (1,056 commands) |
| | `src/plugins/fig_completion_provider.cpp` | Fig/Amazon Q JSON spec loader (715 commands) |
| | `src/plugins/manpage_completion_provider.cpp` | `--help` parsing fallback |
| | `src/plugins/sqlite_history_provider.cpp` | SQLite history + Atuin bridge |
| | `src/plugins/theme_provider.cpp` | Theme loader + 5 bundled TOML palettes |
| | `src/plugins/starship_prompt_provider.cpp` | Optional Starship prompt integration |
| | `src/plugins/alias_suggest_provider.cpp` | "You have an alias for this" reminders |
| | `src/plugins/safety_hook_provider.cpp` | Destructive-command detection |
| | `src/plugins/ai_error_hook_provider.cpp` | Automatic AI error recovery |
| **Util** | `src/util/config_resolver.cpp` | XDG-aware filesystem paths (`~/.tash/`, `$XDG_CONFIG_HOME`) |
| | `src/util/benchmark.cpp` | Staged startup benchmark (`tash --benchmark`) |
| | `src/util/theme.cpp` / `colors.cpp` | Color palette + ANSI wrappers |
| | `src/util/history.cpp` / `frecency.cpp` | Plain-text history + `z` frecency store |
| **AI** | `src/ai/ai_handler.cpp` | `@ai` command routing and unified handler |
| | `src/ai/llm_client.cpp` | Multi-provider LLM client (Gemini, OpenAI, Ollama) with streaming |
| | `src/ai/ai_config.cpp` | Provider config, API keys, rate limiter, usage tracking |
| | `src/ai/contextual_ai.cpp` | `?` suffix routing with project-type and git context |
| | `src/ai/context_suggest.cpp` | Context-aware autosuggestion engine |
| **Entry** | `src/repl.cpp` | REPL loop, replxx setup, heredoc reader |

## Color Palette

Tash uses the [Catppuccin Mocha](https://catppuccin.com) palette — a warm, soothing dark theme:

| Element | Color | Catppuccin Name |
|---------|-------|-----------------|
| Valid command | `#a6e3a1` | Green |
| Builtin command | `#94e2d5` | Teal |
| Invalid command | `#f38ba8` | Red |
| Strings | `#f9e2af` | Yellow |
| Variables | `#89dceb` | Sky |
| Operators | `#cba6f7` | Mauve |
| Redirections | `#fab387` | Peach |
| Comments | `#6c7086` | Overlay0 |
| Banner | `#b4befe` | Lavender |
| Git branch | `#cba6f7` | Mauve |
| Prompt arrow (ok) | `#a6e3a1` | Green |
| Prompt arrow (err) | `#f38ba8` | Red |

## Testing

```sh
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure -V
```

779 tests across 65 test files (24 unit + 41 integration) using Google Test:
- **Core shell** — tokenizer, parser, variable/command expansion, redirections, heredocs, subshells, per-segment pipeline redirections, operators, aliases, auto-cd, `z` frecency, glob edges, unicode paths
- **Builtins** — full matrix of the builtin dispatch table, plus focused suites for `cd`, `source`, `pushd/popd`, `history`, `trap`, `session`, `copy`/`paste`, `explain`, `theme`, `config sync`
- **Process & signals** — fork/exec, job control edges, SIGCHLD stress (many short jobs, cap-fill-and-drain), signal handling robustness
- **Plugins** — registry dispatch/priority/dedup, Fish + Fig completion parsers, man-page `--help` fallback, SQLite history (record, search, migration, large history), themes, Starship, alias reminders, safety hook, AI error recovery
- **UI** — fuzzy finder scoring, inline docs, block renderer, rich output (OSC 8 + tables), startup benchmark, clipboard (base64 + OSC 52 + multi-line detection)
- **AI** — parser, key management, Gemini/OpenAI/Ollama JSON builders/parsers, LLM factory, rate limiter, retry logic, context suggestions, `?` suffix routing, live-provider smoke tests
- **Property + stress** — parser property tests, bulletproofing pass (stress + unicode + property), libFuzzer parser harness with a coverage threshold gate in CI

## Contributing

Contributions are welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for how to get started.

If you find Tash useful, please consider giving it a star — it helps others discover the project.

## License

MIT License — Copyright (c) 2020 Amir Mohammad Tavakkoli
