# Tash (Tavakkoli's Shell)

A lightweight Unix shell written in C++ that supports command chaining, background job control, output redirection, colored output, command history, and tab auto-completion.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Features

| Feature | Supported |
|---------|-----------|
| Foreground commands | Yes |
| Background commands (`bg`) | Yes |
| Pipes (`\|`) | Yes |
| Output redirection (`>`, `>>`) | Yes |
| Input redirection (`<`) | Yes |
| Command chaining (`&&`, `\|\|`, `;`) | Yes |
| Environment variables (`$VAR`, `export`, `unset`) | Yes |
| Glob expansion (`*`, `?`, `[...]`) | Yes |
| Command history (up/down arrows, `history`) | Yes |
| Tab auto-completion | Yes |
| Colored prompt & output | Yes |
| Built-in commands (`cd`, `pwd`, `exit`, `history`, `export`, `unset`) | Yes |
| Background job control (`bglist`, `bgkill`, `bgstop`, `bgstart`) | Yes |
| Signal handling (Ctrl+C, Ctrl+D) | Yes |
| Cross-platform (Linux + macOS) | Yes |

## Installation

### Prerequisites

**Linux (Debian/Ubuntu):**

```sh
sudo apt update
sudo apt-get install libreadline-dev
```

**macOS:**

readline comes pre-installed via libedit. No extra dependencies are needed.

### Build

Compile directly with g++:

```sh
g++ main.cpp colors.cpp -lreadline -o shell.out -std=c++11
```

Or use CMake:

```sh
cmake -B build && cmake --build build
```

### Run

```sh
./shell.out
```

## Usage Examples

**Pipes** -- chain commands together:

```sh
ls | grep cpp | wc -l
```

**Redirection** -- redirect input and output:

```sh
sort < input.txt > output.txt
```

**Environment variables** -- set and use variables:

```sh
export NAME=world && echo $NAME
```

**Glob expansion** -- match files by pattern:

```sh
ls *.cpp
```

**Conditional operators** -- run commands based on success or failure:

```sh
make || echo "build failed"
```

**Background jobs** -- run a process in the background and list active jobs:

```sh
bg sleep 60 && bglist
```

## Built-in Commands

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory. With no argument, goes to `$HOME`. Supports `~`. |
| `pwd` | Print the current working directory. |
| `exit` | Exit the shell. |
| `history` | Show command history (up/down arrows also work). |
| `export VAR=value` | Set an environment variable. |
| `unset VAR` | Remove an environment variable. |
| `bg <command>` | Run a command in the background. |
| `bglist` | List all background jobs. |
| `bgkill <n>` | Terminate background job number *n*. |
| `bgstop <n>` | Pause (stop) background job number *n*. |
| `bgstart <n>` | Resume a stopped background job number *n*. |

## Architecture

Tash follows the classic **fork-exec** pattern used by Unix shells:

1. The main loop reads input via GNU Readline (providing history and tab completion).
2. Input is tokenized and parsed, splitting on operators like `&&`.
3. For each command the shell forks a child process, then calls `execvp` to replace the child with the requested program.
4. The parent process waits for foreground children via `waitpid`, or tracks background children in an internal process table.
5. Built-in commands (`cd`, `pwd`, `exit`, job control) are executed directly in the shell process -- no fork is needed.
6. Signal handling intercepts Ctrl+C so it does not kill the shell itself.

## License

MIT License

Copyright (c) 2020 Amir Mohammad Tavakkoli

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
