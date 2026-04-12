#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

extern std::string shell_binary;

struct ShellResult {
    std::string output;
    int exit_code;
};

inline ShellResult run_shell(const std::string &input) {
    char tmpfile[] = "/tmp/tash_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (write(fd, input.c_str(), input.size())) {}
    close(fd);

    std::string cmd = shell_binary + " < " + tmpfile + " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    int status = pclose(pipe);
    int exit_code = WEXITSTATUS(status);

    unlink(tmpfile);
    return {output, exit_code};
}

inline ShellResult run_shell_script(const std::string &script_path) {
    std::string cmd = shell_binary + " " + script_path + " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    int status = pclose(pipe);
    int exit_code = WEXITSTATUS(status);

    return {output, exit_code};
}

inline std::string read_file(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline int get_file_perms(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return st.st_mode & 0777;
}

// Count occurrences of a substring in a string.
// On Linux, GNU readline echoes input to stdout when stdin is a file,
// so the command text appears once in output. If the command actually
// runs, its output adds a second occurrence.
inline int count_occurrences(const std::string &haystack, const std::string &needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

#endif // TEST_HELPERS_H
