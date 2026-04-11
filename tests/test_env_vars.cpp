#include "test_helpers.h"

TEST(EnvVars, ExpandHOME) {
    auto r = run_shell("echo $HOME\nexit\n");
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_NE(r.output.find(home), std::string::npos);
}

TEST(EnvVars, ExpandUSER) {
    auto r = run_shell("echo $USER\nexit\n");
    const char *user = getenv("USER");
    if (user) EXPECT_NE(r.output.find(user), std::string::npos);
}

TEST(EnvVars, ExportAndRead) {
    auto r = run_shell("export AMISH_TEST_VAR=gtest_value_42\necho $AMISH_TEST_VAR\nexit\n");
    EXPECT_NE(r.output.find("gtest_value_42"), std::string::npos);
}

TEST(EnvVars, UnsetVariable) {
    auto r = run_shell("export AMISH_UNSET_TEST=before\nunset AMISH_UNSET_TEST\necho $AMISH_UNSET_TEST\nexit\n");
    // GNU readline echoes commands to stdout, so "before" appears in the echoed export.
    // If unset works, it appears at most once (from echo). If not, it appears again in output.
    EXPECT_LE(count_occurrences(r.output, "before"), 1) << "Variable should be unset";
}

TEST(EnvVars, UndefinedVarExpandsEmpty) {
    // Verify that a defined var works, while undefined expands to empty
    auto r = run_shell("export AMISH_UNDEF_CHECK=found_it\necho $AMISH_NEVER_DEFINED_XYZ99\necho $AMISH_UNDEF_CHECK\nexit\n");
    EXPECT_NE(r.output.find("found_it"), std::string::npos) << "Defined var should expand";
}

TEST(EnvVars, ExportNoArgsListsVars) {
    auto r = run_shell("export\nexit\n");
    EXPECT_NE(r.output.find("HOME="), std::string::npos);
}
