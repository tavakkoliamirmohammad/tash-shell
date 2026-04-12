# Tash — A Modern Unix Shell

A feature-rich Unix shell written in C++ with syntax highlighting, autosuggestions, smart completions, and a Catppuccin color palette. Built as a deep exploration of systems programming — from `fork`/`exec` to signal handling to interactive line editing.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build](https://github.com/tavakkoliamirmohammad/tash-shell/actions/workflows/build.yml/badge.svg)](https://github.com/tavakkoliamirmohammad/tash-shell/actions)
[![Tests](https://img.shields.io/badge/tests-200%20passing-brightgreen)](https://github.com/tavakkoliamirmohammad/tash-shell/actions)

<!-- TODO: Replace with actual screenshot/GIF of tash in action -->
<!-- ![tash demo](docs/demo.gif) -->

## Highlights

- **Syntax highlighting** — commands glow green if valid, red if not, as you type
- **Autosuggestions** — gray ghost text from history, press `Right` to accept
- **"Did you mean?"** — typo `gti` suggests `git` via Damerau-Levenshtein distance
- **Smart completions** — Tab completes builtins, PATH commands, git/docker subcommands, `$VAR` names
- **Catppuccin Mocha** — a warm, classy color palette across the entire shell
- **200 automated tests** — Google Test suite covering every feature

## Features

| Category | Features |
|----------|----------|
| **Interactive** | Syntax highlighting, autosuggestions (ghost text), `Tab` completion, `Right` to accept hint, `Alt+Right` to accept one word, `Alt+.` to insert last argument |
| **Execution** | Pipes (`\|`), command substitution (`$(cmd)`), script execution (`tash script.sh`) |
| **Redirection** | stdout (`>`, `>>`), stdin (`<`), stderr (`2>`, `2>&1`) |
| **Operators** | `&&` (and), `\|\|` (or), `;` (sequential) |
| **Variables** | `$VAR`, `${VAR}`, `$?` (exit status), `$$` (PID), `export`, `unset` |
| **Expansion** | Glob (`*`, `?`, `[...]`), tilde (`~`), command substitution (`$(...)`) |
| **Navigation** | `cd`, `cd -`, `pushd`/`popd`/`dirs`, **auto-cd** (type directory name), **`z`** (frecency jump) |
| **Job Control** | `bg`, `fg`, `bglist`, `bgkill`, `bgstop`, `bgstart` |
| **Aliases** | `alias ll='ls -la'`, `unalias`, expansion before execution |
| **History** | Persistent `~/.tash_history`, dedup, ignore-space, `!!`, `!n`, arrow navigation |
| **Multiline** | Auto-continue on unclosed quotes or trailing `\|`/`&&`, backslash continuation |
| **Prompt** | Catppuccin-themed two-line prompt with git branch, dirty/clean status (`+*?`), exit code indicator (green/red `❯`), command duration |
| **Safety** | Ctrl+D protection (double-press), bracketed paste, SIGINT handling |
| **Config** | `~/.tashrc` loaded on startup |
| **Platform** | Linux (Ubuntu, Fedora, Alpine) and macOS (Intel + ARM) |

## Quick Start

```sh
# Build
cmake -B build && cmake --build build

# Run
./build/tash.out
```

### Prerequisites

No external dependencies needed. The build system fetches [replxx](https://github.com/AmokHuginnsson/replxx) (line editor) and [Google Test](https://github.com/google/googletest) automatically via CMake FetchContent.

### Install System-Wide

```sh
curl -sSL https://raw.githubusercontent.com/tavakkoliamirmohammad/tash-shell/master/install.sh | bash
```

Or with Homebrew:
```sh
brew install --formula Formula/tash.rb
```

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

## Architecture

```
Input → Replxx (highlighting + hints + completion)
  → History Expansion (!! / !n)
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

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, replxx setup, command execution loop |
| `parser.cpp` | Tokenizer, variable/command expansion, redirection parsing |
| `builtins.cpp` | Dispatch table for 22 built-in commands |
| `process.cpp` | fork/exec, pipelines, background processes |
| `completion.cpp` | Tab completion (builtins, PATH, git/docker, $VAR) |
| `highlight.cpp` | Syntax highlighting + autosuggestion hints |
| `suggest.cpp` | "Did you mean?" via Damerau-Levenshtein distance |
| `history.cpp` | Persistent history with dedup and ignore-space |
| `frecency.cpp` | Frecency-based directory tracking for `z` |
| `prompt.cpp` | Two-line prompt with git status and command duration |
| `colors.cpp` | ANSI color wrapper functions |
| `theme.h` | Catppuccin Mocha color palette definitions |

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

200 tests across 16 test files using Google Test:
- **96 unit tests** — tokenizer, parser, variable expansion, redirections, command suggestions, is_input_complete, frecency, command existence
- **104 integration tests** — pipes, redirection, operators, aliases, scripts, history, auto-cd, z command, "did you mean?", multiline, Ctrl-C/D, git prompt, theme

## License

MIT License — Copyright (c) 2020 Amir Mohammad Tavakkoli
