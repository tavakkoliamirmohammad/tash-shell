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
    char tmpfile[] = "/tmp/amish_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    write(fd, input.c_str(), input.size());
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

#endif // TEST_HELPERS_H
