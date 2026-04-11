#include "test_helpers.h"

TEST(Redirect, FileCreatedWithCorrectPermissions) {
    std::string testfile = "/tmp/amish_gtest_perms_" + std::to_string(getpid()) + ".txt";
    run_shell("echo perm_test > " + testfile + "\nexit\n");
    EXPECT_EQ(read_file(testfile), "perm_test\n");
    EXPECT_EQ(get_file_perms(testfile), 0644);
    unlink(testfile.c_str());
}

TEST(Redirect, OverwriteExistingFile) {
    std::string testfile = "/tmp/amish_gtest_overwrite_" + std::to_string(getpid()) + ".txt";
    run_shell("echo first > " + testfile + "\nexit\n");
    run_shell("echo second > " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("second"), std::string::npos);
    EXPECT_EQ(content.find("first"), std::string::npos);
    unlink(testfile.c_str());
}

TEST(Redirect, AppendToFile) {
    std::string testfile = "/tmp/amish_gtest_append_" + std::to_string(getpid()) + ".txt";
    run_shell("echo line1 > " + testfile + "\nexit\n");
    run_shell("echo line2 >> " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("line1"), std::string::npos);
    EXPECT_NE(content.find("line2"), std::string::npos);
    unlink(testfile.c_str());
}

TEST(Redirect, InputFromFile) {
    std::string infile = "/tmp/amish_gtest_input_" + std::to_string(getpid()) + ".txt";
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
    std::string testfile = "/tmp/amish_gtest_append2_" + std::to_string(getpid()) + ".txt";
    run_shell("echo original > " + testfile + "\nexit\n");
    run_shell("echo appended >> " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("original"), std::string::npos) << "Original content should be preserved";
    EXPECT_NE(content.find("appended"), std::string::npos) << "Appended content should be present";
    unlink(testfile.c_str());
}
