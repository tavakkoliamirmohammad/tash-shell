#include "test_helpers.h"

TEST(Pipes, SinglePipe) {
    auto r = run_shell("echo hello | cat\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}

TEST(Pipes, PipeWithGrep) {
    auto r = run_shell("echo -e 'aaa\\nbbb\\nccc' | grep bbb\nexit\n");
    EXPECT_NE(r.output.find("bbb"), std::string::npos);
}

TEST(Pipes, TriplePipe) {
    auto r = run_shell("echo hello | cat | cat\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}

TEST(Pipes, PipeWithAndOperator) {
    auto r = run_shell("echo hello | cat && echo world\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
    EXPECT_NE(r.output.find("world"), std::string::npos);
}
