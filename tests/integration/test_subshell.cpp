// Subshell `(...)` tests: isolation, redirects, nesting.

#include "test_helpers.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
std::string unique_tmp(const std::string &prefix) {
    return "/tmp/tash_subshell_" + prefix + "_" + std::to_string(getpid());
}
} // namespace

TEST(Subshell, CdInsideDoesNotLeak) {
    auto r = run_shell(
        "cd /tmp\n"
        "(cd /; echo subshell_cwd=$(pwd))\n"
        "echo parent_cwd=$(pwd)\n"
        "exit\n");
    EXPECT_NE(r.output.find("subshell_cwd=/"),  std::string::npos);
    // Parent kept /tmp (real path is /private/tmp on macOS — either is
    // acceptable as long as it contains /tmp and is NOT bare "/").
    EXPECT_NE(r.output.find("parent_cwd="),     std::string::npos);
    EXPECT_EQ(r.output.find("parent_cwd=/\n"),  std::string::npos);
}

TEST(Subshell, ExportInsideDoesNotLeak) {
    auto r = run_shell(
        "(export SUBSHELL_LEAK_PROBE=inside_value)\n"
        "echo outside:$SUBSHELL_LEAK_PROBE\n"
        "exit\n");
    EXPECT_NE(r.output.find("outside:"), std::string::npos);
    EXPECT_EQ(r.output.find("inside_value"), std::string::npos);
}

TEST(Subshell, RedirectsOutputOfGroup) {
    std::string path = unique_tmp("redir");
    run_shell("(echo first; echo second) > " + path + "\nexit\n");
    std::string content = read_file(path);
    unlink(path.c_str());
    EXPECT_NE(content.find("first"),  std::string::npos);
    EXPECT_NE(content.find("second"), std::string::npos);
}

TEST(Subshell, SemicolonsInsideAreRespected) {
    auto r = run_shell(
        "(echo one; echo two; echo three)\n"
        "exit\n");
    EXPECT_NE(r.output.find("one"),   std::string::npos);
    EXPECT_NE(r.output.find("two"),   std::string::npos);
    EXPECT_NE(r.output.find("three"), std::string::npos);
}

TEST(Subshell, NestedSubshells) {
    auto r = run_shell(
        "(echo outer; (echo inner))\n"
        "exit\n");
    EXPECT_NE(r.output.find("outer"), std::string::npos);
    EXPECT_NE(r.output.find("inner"), std::string::npos);
}

TEST(Subshell, ExitStatusPropagates) {
    // `false` fails; the subshell's final exit status becomes the
    // parent's $?. We check via the && / || contract.
    auto r = run_shell(
        "(false) && echo should_not_run\n"
        "(false) || echo and_runs\n"
        "exit\n");
    EXPECT_EQ(r.output.find("should_not_run"), std::string::npos);
    EXPECT_NE(r.output.find("and_runs"),        std::string::npos);
}

TEST(Subshell, UnmatchedParenReportsError) {
    auto r = run_shell(
        "(echo never_runs\n"
        "echo after_error\n"
        "exit\n");
    EXPECT_NE(r.output.find("unmatched"), std::string::npos);
    EXPECT_NE(r.output.find("after_error"), std::string::npos);
}

TEST(Subshell, InPipelineReportsError) {
    auto r = run_shell(
        "(echo a; echo b) | grep a\n"
        "echo marker_after\n"
        "exit\n");
    EXPECT_NE(r.output.find("not yet supported"), std::string::npos);
    EXPECT_NE(r.output.find("marker_after"),       std::string::npos);
}

TEST(Subshell, InScriptFile) {
    std::string script = unique_tmp("script") + ".sh";
    {
        std::ofstream f(script);
        f << "cd /tmp\n"
          << "(cd /; echo subshell_cwd=$(pwd))\n"
          << "echo parent_cwd=$(pwd)\n";
    }
    auto r = run_shell_script(script);
    unlink(script.c_str());
    EXPECT_NE(r.output.find("subshell_cwd=/"),  std::string::npos);
    // Parent remains in /tmp (real path may be /private/tmp on macOS).
    EXPECT_NE(r.output.find("parent_cwd="),     std::string::npos);
    EXPECT_EQ(r.output.find("parent_cwd=/\n"),  std::string::npos);
}
