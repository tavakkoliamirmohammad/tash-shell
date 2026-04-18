#include "test_helpers.h"

// End-to-end smoke of the `help` builtin: spawn tash, run `help`,
// verify a few known builtin names appear in its output.

TEST(Help, ListsKnownBuiltins) {
    auto r = run_shell("help\nexit\n");
    EXPECT_NE(r.output.find("exit"), std::string::npos);
    EXPECT_NE(r.output.find("cd"),   std::string::npos);
    EXPECT_NE(r.output.find("help"), std::string::npos);
}

TEST(Help, ShowsUsageForNamedBuiltin) {
    auto r = run_shell("help cd\nexit\n");
    EXPECT_NE(r.output.find("usage: cd"), std::string::npos);
}

TEST(Help, UnknownBuiltinReportsError) {
    auto r = run_shell("help totally_not_a_builtin\nexit\n");
    EXPECT_NE(r.output.find("no such builtin"), std::string::npos);
}
