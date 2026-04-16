// Matrix tests covering shell builtins across common shell contexts
// (pipeline, redirect, &&, ;, script-file). These are latent-bug nets:
// a builtin that only works in the simplest case (typed alone) will
// be caught here before users hit it in a real workflow.

#include "test_helpers.h"

#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::string unique_tmp(const std::string &prefix) {
    return "/tmp/tash_matrix_" + prefix + "_" + std::to_string(getpid());
}

// ─────────────────────────────────────────────────────────────────────────
// pwd — the simplest builtin; anything that fails here fails everywhere.
// ─────────────────────────────────────────────────────────────────────────

TEST(BuiltinMatrix, PwdInPipeline) {
    auto r = run_shell("pwd | cat\nexit\n");
    EXPECT_NE(r.output.find("/"), std::string::npos);
    EXPECT_EQ(r.output.find("pwd: No such file or directory"),
              std::string::npos);
}

TEST(BuiltinMatrix, PwdWithRedirect) {
    std::string path = unique_tmp("pwd");
    run_shell("pwd > " + path + "\nexit\n");
    std::string content = read_file(path);
    unlink(path.c_str());
    EXPECT_NE(content.find("/"), std::string::npos);
}

TEST(BuiltinMatrix, PwdInAndChain) {
    auto r = run_shell("pwd && echo matrix_after_pwd\nexit\n");
    EXPECT_NE(r.output.find("matrix_after_pwd"), std::string::npos);
}

TEST(BuiltinMatrix, PwdInSemicolonSequence) {
    auto r = run_shell("pwd ; echo matrix_after_semi\nexit\n");
    EXPECT_NE(r.output.find("matrix_after_semi"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────
// alias / history — historically common builtins users pipe.
// ─────────────────────────────────────────────────────────────────────────

TEST(BuiltinMatrix, AliasInPipeline) {
    auto r = run_shell(
        "alias abc=\"echo ok\"\n"
        "alias | grep abc\n"
        "exit\n");
    EXPECT_NE(r.output.find("abc"), std::string::npos);
    EXPECT_EQ(r.output.find("alias: No such file or directory"),
              std::string::npos);
}

TEST(BuiltinMatrix, HistoryInPipeline) {
    auto r = run_shell(
        "echo hist_probe\n"
        "history | tail -5\n"
        "exit\n");
    EXPECT_EQ(r.output.find("history: No such file or directory"),
              std::string::npos);
    EXPECT_EQ(r.output.find("history: command not found"),
              std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────
// theme (from PR #73)
// ─────────────────────────────────────────────────────────────────────────

TEST(BuiltinMatrix, ThemeListInPipeline) {
    auto r = run_shell("theme list | grep mocha\nexit\n");
    EXPECT_NE(r.output.find("mocha"), std::string::npos);
}

TEST(BuiltinMatrix, ThemeCurrentWithRedirect) {
    std::string path = unique_tmp("theme");
    run_shell("theme current > " + path + "\nexit\n");
    std::string content = read_file(path);
    unlink(path.c_str());
    EXPECT_FALSE(content.empty());
}

TEST(BuiltinMatrix, ThemeFromScriptFile) {
    std::string script = unique_tmp("theme_script") + ".sh";
    {
        std::ofstream f(script);
        f << "theme current\n";
    }
    auto r = run_shell_script(script);
    unlink(script.c_str());
    EXPECT_FALSE(r.output.empty());
    EXPECT_EQ(r.output.find("theme: No such file or directory"),
              std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────
// explain (from PR #81)
// ─────────────────────────────────────────────────────────────────────────

TEST(BuiltinMatrix, ExplainInPipeline) {
    auto r = run_shell("explain tar -x | head -5\nexit\n");
    EXPECT_NE(r.output.find("Extract"), std::string::npos);
    EXPECT_EQ(r.output.find("explain: No such file or directory"),
              std::string::npos);
}

TEST(BuiltinMatrix, ExplainWithRedirect) {
    std::string path = unique_tmp("explain");
    run_shell("explain grep -i > " + path + "\nexit\n");
    std::string content = read_file(path);
    unlink(path.c_str());
    EXPECT_FALSE(content.empty());
}

TEST(BuiltinMatrix, ExplainInAndChain) {
    auto r = run_shell("explain ls && echo explained_ok\nexit\n");
    EXPECT_NE(r.output.find("explained_ok"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────
// Script files
// ─────────────────────────────────────────────────────────────────────────

TEST(BuiltinMatrix, BuiltinsInScriptFile) {
    std::string script = unique_tmp("script_builtins") + ".sh";
    {
        std::ofstream f(script);
        f << "pwd\n"
          << "alias foo=\"echo FOO\"\n"
          << "alias\n";
    }
    auto r = run_shell_script(script);
    unlink(script.c_str());
    EXPECT_NE(r.output.find("/"), std::string::npos);
    EXPECT_NE(r.output.find("foo="), std::string::npos);
}

} // namespace
