#include "test_helpers.h"

TEST(Alias, AliasExpandsCommand) {
    auto r = run_shell("alias ll='ls -la'\nll\nexit\n");
    // ls -la output should contain "total" (typical long-format listing header)
    EXPECT_NE(r.output.find("total"), std::string::npos);
}

TEST(Alias, AliasNoArgsListsAliases) {
    auto r = run_shell("alias myalias='echo test'\nalias\nexit\n");
    EXPECT_NE(r.output.find("alias myalias='echo test'"), std::string::npos);
}

TEST(Alias, UnaliasRemovesAlias) {
    auto r = run_shell("alias greeting='echo hello'\ngreeting\nunalias greeting\ngreeting\nexit\n");
    // "hello" should appear (from the first invocation)
    EXPECT_NE(r.output.find("hello"), std::string::npos);
    // After unalias, "greeting" should fail with "No such file or directory"
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos);
}

TEST(Alias, AliasWithEcho) {
    auto r = run_shell("alias hi='echo hello_world'\nhi\nexit\n");
    EXPECT_NE(r.output.find("hello_world"), std::string::npos);
}

TEST(Alias, AliasWithExtraArgs) {
    // Alias expands the first word; remaining args are appended
    auto r = run_shell("alias e='echo'\ne alias_extra_arg_test\nexit\n");
    EXPECT_NE(r.output.find("alias_extra_arg_test"), std::string::npos);
}
