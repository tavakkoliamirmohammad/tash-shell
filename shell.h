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
#include <unordered_map>
#include <unordered_set>

#include <readline/readline.h>
#include <readline/history.h>

#include "colors.h"

using namespace std;

#define MAX_SIZE 1024

// Global variables (defined in main.cpp)
extern unordered_set<string> colorful_commands;
extern char hostname[MAX_SIZE];

// Utility functions (defined in main.cpp)
void exit_with_message(const string &message, int exit_status);
void write_stderr(const string &message);
void write_stdout(const string &message);
void show_error_command(const vector<char *> &args);

// Prompt (defined in prompt.cpp)
string write_shell_prefix();

// Parser (defined in parser.cpp)
string &rtrim(string &s, const char *t = " \t\n\r\f\v");
string &ltrim(string &s, const char *t = " \t\n\r\f\v");
string &trim(string &s, const char *t = " \t\n\r\f\v");
vector<string> tokenize_string(string line, const string &delimiter);

// Builtins (defined in builtins.cpp)
void change_directory(vector<char *> args);
void show_current_directory(vector<char *> args);
void show_background_process(unordered_map<pid_t, string> &background_processes);
pid_t get_nth_background_process(unordered_map<pid_t, string> &background_processes, int n);
void background_process_signal(pid_t pid, int signal);

// Process management (defined in process.cpp)
void foreground_process(vector<char *> args, const string &filename, int flag);
void background_process(vector<char *> args, unordered_map<pid_t, string> &background_processes_list,
                        int maximum_background_process, const string &filename, int flag);
void check_background_process_finished(unordered_map<pid_t, string> &background_processes);

#endif /* SHELL_H */
