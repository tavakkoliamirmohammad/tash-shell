#include "test_helpers.h"

TEST(Comments, InlineCommentStripped) {
    // Use file side-effect to avoid readline echo issues on Linux
    std::string testfile = "/tmp/tash_inline_comment_" + std::to_string(getpid()) + ".txt";
    run_shell("echo hello # this is a comment > " + testfile + "\nexit\n");
    // Actually, the comment strips "> file" too. Use a different approach:
    // redirect before the comment
    run_shell("echo hello > " + testfile + " # this is a comment\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("hello"), std::string::npos);
    EXPECT_EQ(content.find("comment"), std::string::npos);
    unlink(testfile.c_str());
}

TEST(Comments, FullLineComment) {
    auto r = run_shell("# full line comment\necho works\nexit\n");
    EXPECT_NE(r.output.find("works"), std::string::npos);
}

TEST(Comments, HashInsideDoubleQuotesPreserved) {
    std::string testfile = "/tmp/tash_comment_test_" + std::to_string(getpid()) + ".txt";
    run_shell("echo \"hello # not a comment\" > " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    EXPECT_NE(content.find("hello # not a comment"), std::string::npos);
    unlink(testfile.c_str());
}
