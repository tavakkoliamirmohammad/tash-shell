#include "test_helpers.h"

TEST(Script, ExecuteScriptFile) {
    // Create a temporary script file
    std::string script_path = "/tmp/tash_test_script_" + std::to_string(getpid()) + ".sh";
    {
        std::ofstream f(script_path);
        f << "echo script_works" << std::endl;
    }

    auto r = run_shell_script(script_path);
    EXPECT_NE(r.output.find("script_works"), std::string::npos)
        << "Script output should contain 'script_works', got: " << r.output;
    EXPECT_EQ(r.exit_code, 0);

    unlink(script_path.c_str());
}

TEST(Script, MultipleCommandsInScript) {
    std::string script_path = "/tmp/tash_test_multi_" + std::to_string(getpid()) + ".sh";
    {
        std::ofstream f(script_path);
        f << "echo line_one" << std::endl;
        f << "echo line_two" << std::endl;
    }

    auto r = run_shell_script(script_path);
    EXPECT_NE(r.output.find("line_one"), std::string::npos);
    EXPECT_NE(r.output.find("line_two"), std::string::npos);

    unlink(script_path.c_str());
}

TEST(Script, NonexistentScriptFile) {
    auto r = run_shell_script("/tmp/tash_no_such_file_ever.sh");
    EXPECT_NE(r.exit_code, 0);
}

TEST(Script, SourceCommand) {
    // Create a script that uses source to load a file, then reads the exported variable
    std::string source_file = "/tmp/tash_test_source_" + std::to_string(getpid()) + ".sh";
    std::string marker_file = "/tmp/tash_test_source_marker_" + std::to_string(getpid());
    {
        std::ofstream f(source_file);
        f << "export TASH_SOURCE_VAR=source_value_ok" << std::endl;
    }

    // Run the shell interactively: source the file, then echo the variable to a marker file
    auto r = run_shell("source " + source_file + "\necho $TASH_SOURCE_VAR > " + marker_file + "\nexit\n");

    std::string content = read_file(marker_file);
    EXPECT_NE(content.find("source_value_ok"), std::string::npos)
        << "Sourced variable should be available. Marker content: " << content;

    unlink(source_file.c_str());
    unlink(marker_file.c_str());
}

TEST(Script, SourceMissingArg) {
    auto r = run_shell("source\nexit\n");
    EXPECT_NE(r.output.find("source: missing file argument"), std::string::npos);
}
