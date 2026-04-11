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
    EXPECT_EQ(r.output.find("before"), std::string::npos) << "Variable should be unset";
}

TEST(EnvVars, UndefinedVarExpandsEmpty) {
    auto r = run_shell("echo $DEFINITELY_NOT_SET_12345\nexit\n");
    EXPECT_EQ(r.output.find("DEFINITELY_NOT_SET_12345"), std::string::npos);
}

TEST(EnvVars, ExportNoArgsListsVars) {
    auto r = run_shell("export\nexit\n");
    EXPECT_NE(r.output.find("HOME="), std::string::npos);
}
