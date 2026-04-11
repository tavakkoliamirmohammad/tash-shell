#include "test_helpers.h"
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

// Helper that runs the shell with a custom HOME directory so we don't
// touch the real ~/.tashrc.  We create a temporary directory, write a
// .tashrc into it, then invoke the shell with HOME overridden.
static ShellResult run_shell_with_home(const std::string &home_dir,
                                       const std::string &input) {
    char tmpfile[] = "/tmp/tash_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    write(fd, input.c_str(), input.size());
    close(fd);

    std::string cmd = "HOME=" + home_dir + " " + shell_binary + " < " + tmpfile + " 2>&1";
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

TEST(Tashrc, LoadsRcFileOnStartup) {
    // Create a temporary HOME directory
    std::string tmp_home = "/tmp/tash_rc_test_" + std::to_string(getpid());
    mkdir(tmp_home.c_str(), 0755);

    // Write a .tashrc that exports a variable
    {
        std::ofstream rc(tmp_home + "/.tashrc");
        rc << "export TASH_RC_LOADED=hello_from_tashrc" << std::endl;
        rc.close();
    }

    // Run the shell with custom HOME; echo the variable to verify it was set
    auto r = run_shell_with_home(tmp_home, "echo $TASH_RC_LOADED\nexit\n");
    EXPECT_NE(r.output.find("hello_from_tashrc"), std::string::npos)
        << "Variable exported in .tashrc should be available in session. Output: " << r.output;

    // Cleanup
    unlink((tmp_home + "/.tashrc").c_str());
    rmdir(tmp_home.c_str());
}

TEST(Tashrc, NoRcFileIsOkay) {
    // Create a temporary HOME directory with no .tashrc
    std::string tmp_home = "/tmp/tash_rc_empty_" + std::to_string(getpid());
    mkdir(tmp_home.c_str(), 0755);

    // Shell should start normally without a .tashrc
    auto r = run_shell_with_home(tmp_home, "echo works_fine\nexit\n");
    EXPECT_NE(r.output.find("works_fine"), std::string::npos)
        << "Shell should work normally without .tashrc. Output: " << r.output;

    // Cleanup
    rmdir(tmp_home.c_str());
}

TEST(Tashrc, MultipleCommandsInRc) {
    // Create a temporary HOME directory
    std::string tmp_home = "/tmp/tash_rc_multi_" + std::to_string(getpid());
    mkdir(tmp_home.c_str(), 0755);

    // Write a .tashrc with multiple export commands
    {
        std::ofstream rc(tmp_home + "/.tashrc");
        rc << "export TASH_VAR_A=alpha" << std::endl;
        rc << "export TASH_VAR_B=bravo" << std::endl;
        rc.close();
    }

    // Both variables should be set
    auto r = run_shell_with_home(tmp_home, "echo $TASH_VAR_A $TASH_VAR_B\nexit\n");
    EXPECT_NE(r.output.find("alpha"), std::string::npos)
        << "First var from .tashrc should be set. Output: " << r.output;
    EXPECT_NE(r.output.find("bravo"), std::string::npos)
        << "Second var from .tashrc should be set. Output: " << r.output;

    // Cleanup
    unlink((tmp_home + "/.tashrc").c_str());
    rmdir(tmp_home.c_str());
}
