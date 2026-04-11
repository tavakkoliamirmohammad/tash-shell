#include "test_helpers.h"

TEST(Redirect, FileCreatedWithCorrectPermissions) {
    std::string testfile = "/tmp/tash_gtest_perms_" + std::to_string(getpid()) + ".txt";
    run_shell("echo perm_test > " + testfile + "\nexit\n");
    EXPECT_EQ(read_file(testfile), "perm_test\n");
    EXPECT_EQ(get_file_perms(testfile), 0644);
    unlink(testfile.c_str());
}

TEST(Redirect, OverwriteExistingFile) {
    std::string testfile = "/tmp/tash_gtest_overwrite_" + std::to_string(getpid()) + ".txt";
    run_shell("echo first > " + testfile + "\nexit\n");
    run_shell("echo second > " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("second"), std::string::npos);
    EXPECT_EQ(content.find("first"), std::string::npos);
    unlink(testfile.c_str());
}

TEST(Redirect, AppendToFile) {
    std::string testfile = "/tmp/tash_gtest_append_" + std::to_string(getpid()) + ".txt";
    run_shell("echo line1 > " + testfile + "\nexit\n");
    run_shell("echo line2 >> " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("line1"), std::string::npos);
    EXPECT_NE(content.find("line2"), std::string::npos);
    unlink(testfile.c_str());
}

TEST(Redirect, InputFromFile) {
    std::string infile = "/tmp/tash_gtest_input_" + std::to_string(getpid()) + ".txt";
    { std::ofstream f(infile); f << "cherry\napple\nbanana\n"; }
    auto r = run_shell("sort < " + infile + "\nexit\n");
    EXPECT_NE(r.output.find("apple"), std::string::npos);
    auto apple_pos = r.output.find("apple");
    auto banana_pos = r.output.find("banana");
    if (apple_pos != std::string::npos && banana_pos != std::string::npos)
        EXPECT_LT(apple_pos, banana_pos);
    unlink(infile.c_str());
}

TEST(Redirect, AppendDoesNotOverwrite) {
    std::string testfile = "/tmp/tash_gtest_append2_" + std::to_string(getpid()) + ".txt";
    run_shell("echo original > " + testfile + "\nexit\n");
    run_shell("echo appended >> " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("original"), std::string::npos) << "Original content should be preserved";
    EXPECT_NE(content.find("appended"), std::string::npos) << "Appended content should be present";
    unlink(testfile.c_str());
}

TEST(Redirect, StderrToFile) {
    std::string errfile = "/tmp/tash_gtest_stderr_" + std::to_string(getpid()) + ".txt";
    // ls on a nonexistent path writes an error to stderr
    run_shell("ls /nonexistent_path_for_tash_test 2> " + errfile + "\nexit\n");
    std::string content = read_file(errfile);
    EXPECT_NE(content.find("No such file or directory"), std::string::npos)
        << "stderr should be captured in the file, got: " + content;
    unlink(errfile.c_str());
}

TEST(Redirect, StderrToStdout) {
    // With 2>&1, stderr merges into stdout so the error message appears in output
    auto r = run_shell("ls /nonexistent_path_for_tash_test 2>&1\nexit\n");
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos)
        << "stderr merged into stdout should contain the error, got: " + r.output;
}

TEST(Redirect, StderrToFileDoesNotAffectStdout) {
    std::string outfile = "/tmp/tash_gtest_stdout_" + std::to_string(getpid()) + ".txt";
    std::string errfile = "/tmp/tash_gtest_stderr2_" + std::to_string(getpid()) + ".txt";
    // echo writes to stdout; redirect stdout to outfile and stderr to errfile
    run_shell("echo hello > " + outfile + " 2> " + errfile + "\nexit\n");
    std::string out_content = read_file(outfile);
    std::string err_content = read_file(errfile);
    EXPECT_NE(out_content.find("hello"), std::string::npos)
        << "stdout should go to outfile, got: " + out_content;
    // echo produces no stderr, so errfile should be empty
    EXPECT_TRUE(err_content.empty())
        << "stderr file should be empty for echo, got: " + err_content;
    unlink(outfile.c_str());
    unlink(errfile.c_str());
}
