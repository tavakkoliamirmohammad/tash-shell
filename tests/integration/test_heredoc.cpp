// POSIX heredoc (<<, <<-, quoted delimiter) integration tests.

#include "test_helpers.h"

#include <string>

TEST(Heredoc, BasicBodyGoesToStdin) {
    auto r = run_shell(
        "cat <<EOF\n"
        "alpha\n"
        "beta\n"
        "EOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("alpha"), std::string::npos);
    EXPECT_NE(r.output.find("beta"),  std::string::npos);
}

TEST(Heredoc, UnquotedDelimExpandsVariables) {
    auto r = run_shell(
        "export PROBE=expanded_value\n"
        "cat <<EOF\n"
        "value is $PROBE\n"
        "EOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("value is expanded_value"), std::string::npos);
}

TEST(Heredoc, SingleQuotedDelimDisablesExpansion) {
    auto r = run_shell(
        "export PROBE=should_not_appear\n"
        "cat <<'EOF'\n"
        "literal $PROBE here\n"
        "EOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("literal $PROBE here"), std::string::npos);
    EXPECT_EQ(r.output.find("should_not_appear"),   std::string::npos);
}

TEST(Heredoc, DoubleQuotedDelimAlsoDisablesExpansion) {
    auto r = run_shell(
        "export PROBE=not_expanded\n"
        "cat <<\"EOF\"\n"
        "raw $PROBE text\n"
        "EOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("raw $PROBE text"), std::string::npos);
    EXPECT_EQ(r.output.find("not_expanded"),     std::string::npos);
}

TEST(Heredoc, DashStripsLeadingTabs) {
    // <<- form strips leading tabs from body and terminator line.
    auto r = run_shell(
        "cat <<-EOF\n"
        "\tindented with tab\n"
        "\tEOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("indented with tab"), std::string::npos);
    // Output should not contain the tab character before the text.
    EXPECT_EQ(r.output.find("\tindented"), std::string::npos);
}

TEST(Heredoc, InScriptFile) {
    std::string script = "/tmp/tash_heredoc_script_" +
                         std::to_string(getpid()) + ".sh";
    {
        std::ofstream f(script);
        f << "cat <<END_OF_POEM\n"
          << "roses are red\n"
          << "violets are blue\n"
          << "END_OF_POEM\n";
    }
    auto r = run_shell_script(script);
    unlink(script.c_str());
    EXPECT_NE(r.output.find("roses are red"),    std::string::npos);
    EXPECT_NE(r.output.find("violets are blue"), std::string::npos);
}

TEST(Heredoc, CustomDelimiter) {
    auto r = run_shell(
        "cat <<MY_END_MARKER\n"
        "content line\n"
        "EOF should not end this\n"
        "MY_END_MARKER\n"
        "exit\n");
    EXPECT_NE(r.output.find("content line"), std::string::npos);
    EXPECT_NE(r.output.find("EOF should not end"), std::string::npos);
}

TEST(Heredoc, InPipelineFeedsIntoNextStage) {
    // Heredoc body becomes stdin of the first pipeline stage.
    auto r = run_shell(
        "cat <<EOF | grep line\n"
        "line one\n"
        "line two\n"
        "other\n"
        "EOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("line one"), std::string::npos);
    EXPECT_NE(r.output.find("line two"), std::string::npos);
    EXPECT_EQ(r.output.find("other"),    std::string::npos);
}

TEST(Heredoc, EmptyBody) {
    auto r = run_shell(
        "cat <<EOF\n"
        "EOF\n"
        "echo after\n"
        "exit\n");
    // Empty body is valid; cat produces no output and subsequent
    // commands still run.
    EXPECT_NE(r.output.find("after"), std::string::npos);
}

TEST(Heredoc, CommandSubstitutionInBody) {
    auto r = run_shell(
        "cat <<EOF\n"
        "got $(echo inner_probe)\n"
        "EOF\n"
        "exit\n");
    EXPECT_NE(r.output.find("got inner_probe"), std::string::npos);
}
