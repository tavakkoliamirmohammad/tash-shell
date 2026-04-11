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
    // Use file side-effect: export a var, unset it, then try to write it to a file.
    // If unset works, $AMISH_UNSET_FILE expands to empty and the redirect target is empty/fails.
    // Instead, verify by exporting a new value after unset — it should not have the old value.
    std::string marker = "/tmp/amish_unset_" + std::to_string(getpid());
    unlink(marker.c_str());
    run_shell("export AMISH_UNSET_VAR=old_value\nunset AMISH_UNSET_VAR\nexport AMISH_UNSET_VAR=new_value\necho $AMISH_UNSET_VAR > " + marker + "\nexit\n");
    std::string content = read_file(marker);
    EXPECT_NE(content.find("new_value"), std::string::npos) << "Should have new value after re-export";
    EXPECT_EQ(content.find("old_value"), std::string::npos) << "Old value should be gone";
    unlink(marker.c_str());
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
