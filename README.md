# Tash — A Modern Unix Shell

A feature-rich Unix shell written in C++ with pipes, redirection, job control, command substitution, scripting, aliases, and more. Built as a deep exploration of systems programming — from `fork`/`exec` to signal handling to readline integration.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build](https://github.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface/actions/workflows/build.yml/badge.svg)](https://github.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface/actions)

## Features

| Category | Features |
|----------|----------|
| **Execution** | Pipes (`\|`), command substitution (`$(cmd)`), script execution (`tash script.sh`) |
| **Redirection** | stdout (`>`, `>>`), stdin (`<`), stderr (`2>`, `2>&1`) |
| **Operators** | `&&` (and), `\|\|` (or), `;` (sequential) |
| **Variables** | `$VAR`, `${VAR}`, `$?` (exit status), `$$` (PID), `export`, `unset` |
| **Expansion** | Glob (`*`, `?`, `[...]`), tilde (`~`), command substitution (`$(...)`) |
| **Job Control** | `bg`, `fg`, `bglist`, `bgkill`, `bgstop`, `bgstart` |
| **Navigation** | `cd`, `cd -`, `pushd`, `popd`, `dirs` |
| **Aliases** | `alias ll='ls -la'`, `unalias`, expansion before execution |
| **History** | Up/down arrows, `history`, `!!` (repeat last), `!n` (repeat nth) |
| **Scripting** | `source file`, `tash script.sh`, `#` comments, `\` line continuation |
| **Completion** | Tab completion for built-in commands and file paths |
| **Prompt** | Two-line colored prompt with git branch, username, and path |
| **Config** | `~/.tashrc` loaded on startup |
| **Utilities** | `which`, `type`, `clear`, Ctrl+L, Ctrl+C, Ctrl+D |
| **Platform** | Linux (Ubuntu, Fedora, Alpine) and macOS (Intel + ARM) |

## Quick Start

```sh
# Build
cmake -B build && cmake --build build

# Run
./build/tash.out
```

### Prerequisites

**Linux:** `sudo apt install libreadline-dev` (or `dnf install readline-devel` on Fedora)

**macOS:** No extra dependencies needed (uses built-in libedit)

### Install System-Wide

```sh
curl -sSL https://raw.githubusercontent.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface/master/install.sh | bash
```

Or with Homebrew:
```sh
brew install --formula Formula/tash.rb
```

## Usage

```
╭─ amir in ~/projects/tash on  master
╰─❯ ls | grep cpp | wc -l
       6

╭─ amir in ~/projects/tash on  master
╰─❯ echo "today is $(date +%A)"
today is Friday

╭─ amir in ~/projects/tash on  master
╰─❯ export NAME=world && echo $NAME
world

╭─ amir in ~/projects/tash on  master
╰─❯ alias ll='ls -la'
╭─ amir in ~/projects/tash on  master
╰─❯ ll *.cpp

╭─ amir in ~/projects/tash on  master
╰─❯ false || echo "fallback runs"
fallback runs

╭─ amir in ~/projects/tash on  master
╰─❯ bg sleep 60 && bglist
Background process with 12345 Executing
(1) sleep
Total Background Jobs: 1

╭─ amir in ~/projects/tash on  master
╰─❯ sort < input.txt > output.txt 2> errors.txt
```

## Built-in Commands

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory. `cd -` returns to previous. `cd ~` goes home. |
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
Input → Readline → History Expansion → Parse Operators (&&, ||, ;)
  → For each command:
      Strip Comments → Expand Variables ($VAR, $?, $$)
      → Command Substitution $(...)  → Parse Redirections (>, >>, <, 2>)
      → Split Pipes → Tokenize → Strip Quotes → Expand Aliases
      → Expand Globs → Execute (fork/exec or builtin)
```

## Testing

```sh
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure -V
```

142+ tests across 15 test files using Google Test, covering tokenizer, pipes, redirection, operators, environment variables, aliases, scripts, history, and more.

## License

MIT License — Copyright (c) 2020 Amir Mohammad Tavakkoli
