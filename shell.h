#ifndef SHELL_H
#define SHELL_H

#include <unistd.h>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <regex>
#include <glob.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <cstdlib>
#include <fstream>

#include "colors.h"

using namespace std;

// ── Constants ───────────────────────────────────────────────────
#define MAX_SIZE 1024

#ifdef __APPLE__
#define COLOR_FLAG "-G"
#else
#define COLOR_FLAG "--color=auto"
#endif

// ── Operator types for command separators ───────────────────────

enum OperatorType {
    OP_NONE,
    OP_AND,
    OP_OR,
    OP_SEMICOLON
};

struct CommandSegment {
    string command;
    OperatorType op;
};

// ── Global variable extern declarations ─────────────────────────

extern unordered_set<string> colorful_commands;
extern unordered_map<string, string> aliases;
extern char hostname[MAX_SIZE];
extern volatile sig_atomic_t fg_child_pid;
extern volatile sig_atomic_t sigchld_received;
extern string previous_directory;
extern int last_exit_status;
extern char **environ;

// ── prompt.cpp ──────────────────────────────────────────────────

string get_git_branch();
string write_shell_prefix();

// ── parser.cpp ──────────────────────────────────────────────────

string &rtrim(string &s, const char *t = " \t\n\r\f\v");
string &ltrim(string &s, const char *t = " \t\n\r\f\v");
string &trim(string &s, const char *t = " \t\n\r\f\v");
vector<string> tokenize_string(string line, const string &delimiter);
string expand_variables(const string &input);
string expand_command_substitution(const string &input);
vector<string> expand_globs(const vector<string> &args);
string strip_quotes(const string &s);
vector<CommandSegment> parse_command_line(const string &line);
string expand_history(const string &line);

// ── builtins.cpp ────────────────────────────────────────────────

void change_directory(vector<char *> args);
void show_current_directory(vector<char *> args);
void show_background_process(unordered_map<pid_t, string> &background_processes);
pid_t get_nth_background_process(unordered_map<pid_t, string> &background_processes, int n);
void background_process_signal(pid_t pid, int signal);

// ── process.cpp ─────────────────────────────────────────────────

int foreground_process(vector<char *> args, const string &filename, int flag,
                       const string &input_filename, int input_flag, int append_flag,
                       const string &stderr_filename, int stderr_flag, int stderr_to_stdout);
void background_process(vector<char *> args, unordered_map<pid_t, string> &background_processes_list,
                        int maximum_background_process, const string &filename, int flag,
                        const string &input_filename, int input_flag, int append_flag,
                        const string &stderr_filename, int stderr_flag, int stderr_to_stdout);
void check_background_process_finished(unordered_map<pid_t, string> &background_processes);
void reap_background_processes(unordered_map<pid_t, string> &background_processes);
void execute_pipeline(vector<vector<char *>> &pipeline_args, const string &filename, int redirect_flag);

// ── main.cpp ────────────────────────────────────────────────────

void exit_with_message(const string &message, int exit_status);
void write_stderr(const string &message);
void write_stdout(const string &message);
void show_error_command(const vector<char *> &args);
int execute_single_command(string command, unordered_map<pid_t, string> &background_processes,
                           int maximum_background_process);
void execute_command_line(const vector<CommandSegment> &segments,
                          unordered_map<pid_t, string> &background_processes,
                          int maximum_background_process);
void sigint_handler(int signum);
void sigchld_handler(int signum);
int execute_script_file(const string &path,
                        unordered_map<pid_t, string> &background_processes,
                        int maximum_background_process);

#endif // SHELL_H
