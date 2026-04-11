# Amish (Amir's shell)

This the course project of Operating Systems lectured by Dr. Mohammad R. Moosavi.

## Overview

In this project, we build a command line interpreter or, as it is more commonly known, a shell. To get familiar with the
shell, let's investigate how it works. By typing a command (in response to shell prompt), the shell creates a child
process that executes the command you entered and then prompts for more user input when it has finished.

The shells we will implement will be similar to, but simpler than, the one you run every day in Unix. To find out which
shell you are currently running, you can type ``echo $SHELL`` at a prompt. To dive deeper about functionalities that you
are running shell offers, You may wish to look at the man pages.

## Installation

This project requires [Readline](https://tiswww.case.edu/php/chet/readline/rltop.html) library to run.

```sh
$ sudo apt update
$ sudo apt-get install lib32readline8 lib32readline-dev
```

you can compile the project with following command:

```sh
$ g++ main.cpp colors.cpp -lreadline -o shell.out && ./shell.out
$ ./shell.out
```

## Program Specifications

### Shell

Shell is basically an interactive loop that repeatedly prints a message that is called prompt. After that user enters
the command, the shell parses the input, executes the command specified on that line of input, and waits for the command
to finish. This is repeated until the user types ``exit``. Shell creates a new process for each new command. There are
two advantages to this approach. First, it protects the main shell process from any errors that occur in the command.
Second, it allows for concurrency; that is, multiple commands can be started and allowed to execute simultaneously.

Note that the shell itself does not **implement** any command at all. All it does is find those executables and create a
new process to run them. The shell program fork a child process by calling
the [`fork`](https://man7.org/linux/man-pages/man2/fork.2.html) system call and then executes commands
via [`execvp`](https://linux.die.net/man/3/execvp). Then, the parent process waits for the child to finish its execution
via [`wait`](https://man7.org/linux/man-pages/man2/waitpid.2.html). Whether to run the child process in the background
or foreground depends on the flage provided to `wait` system call.

This is called fork-exec pattern, which is a commonly used technique in Unix whereby an executing process spawns a new
program.

### Multiple Commands

One of the interesting features of a shell is that you can run multiple jobs on a single command line. All you have to
do is to separate your commands with the ``&&`` character on a single command line. For example, if the user
types ``ls && ps && who`` , the jobs should be run all one-by-one in sequential mode. Hence, in our previous
example ( ``ls && ps && who`` ), all three jobs should run: first ``ls``, then ``ps``, then ``who``. After they are
done, you should finally get a prompt back.

``` sh
↪ amir@tavakkoli ~/projects/unix_command_line_interface shell>  ls && ps && who
a.txt  colors.cpp  colors.h  main.cpp  README.md  shell.out
    PID TTY          TIME CMD
  25370 pts/0    00:00:00 zsh
  25412 pts/0    00:00:00 shell.out
  27467 pts/0    00:00:00 ps
amir     :1           2021-02-03 10:38 (:1)
```

### Built-in Commands

Whenever your shell accepts a command, it should check whether the command is a built-in command or not. If it is, it
should not be executed like other programs. Instead, your shell will invoke your implementation of the built-in command.
For example, to implement the exit built-in command, you simply call ``exit(0);`` in your C++ program.

```sh
↪ amir@tavakkoli ~/projects/unix_command_line_interface shell> exit
GoodBye! See you soon!
```

Your shell users will be happy with the ability to change their working directory. Without this feature, your user is
stuck in a single directory. You can call [``getcwd``](https://man7.org/linux/man-pages/man3/getcwd.3.html) to obtain
current working directory.

``` sh
↪ amir@tavakkoli ~/projects/unix_command_line_interface shell>  pwd
/home/amir/projects/unix_command_line_interface
```

Also, by calling [``chdir``](https://man7.org/linux/man-pages/man2/chdir.2.html) you can change directory. When you
run ``cd`` (without arguments), your shell should change the working directory to the path stored in the ``$HOME``
environment variable. You can use ``getenv`` function to obtain any environment variable. Like a typical Unix shell, you
can type ``cd ~`` to go user's home directory.

``` sh
↪ amir@tavakkoli ~/projects/unix_command_line_interface shell> cd
↪ amir@tavakkoli ~ shell> 
```

### Background commands

By placing `bg` before a command you can run the command in the background. To find out which command runs in the
background, you can enter `bglist` command.

```sh
↪ amir@tavakkoli ~/projects/unix_command_line_interface shell> bglist
(1) ping
(2) ping
(3) ping
(4) ping
(5) ping
Total Background Jobs: 5
```

You can stop a background process by executing `bgstop {process_number}` command, In order to continue the background
process, you should use `bgstart {process_number}` command.

### File Output Redirection

Many times, a shell user prefers to send the output of his/her program to a file rather than to the screen. The UNIX
shell provides this nice feature with the ">" character. Formally this is named as redirection of standard output. For
example, if a user types `echo Hello > output.txt` , the output of the `echo` command should be stored in the
file `output.txt`. You can achieve this by duplicating file descriptor via
function [``dup2``](https://www.mkssoftware.com/docs/man3/dup2.3.asp).

### History and Auto-completion

A helpful feature that almost all Unix shell support is the ability to move back and forth between previously entered
command. By the help of readline library, we can support auto-completion and history for the convenience of user.

### Program Errors

When one of child processes catches error, `strerror(errno)` returns the error message that was set by `errno` flag. The
error is redirected to `stderr` (standard error stream).

License
----
MIT License
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Copyright (c) 2020 Amir Mohammad Tavakkoli

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Distribution

### Quick Install (Linux & macOS)

```sh
curl -sSL https://raw.githubusercontent.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface/master/install.sh | bash
```

### Homebrew (macOS & Linux)

```sh
brew install --formula Formula/amish.rb
```

### From Source

```sh
git clone https://github.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface.git
cd UNIX-Command-Line-Interface
g++ main.cpp colors.cpp -lreadline -o amish -std=c++11
sudo install -m 755 amish /usr/local/bin/amish
```
