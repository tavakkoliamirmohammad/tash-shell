// Unit tests for positional parse-error diagnostics (deep-review O7.3).
//
// Each test exercises a parser path that used to silently accept
// malformed input (or emit a bare "tash: <msg>" with no location) and
// checks the new emit_parse_error path produces
//   tash: error: <line>:<col>: <msg>
// pointing at the offending byte. We capture stderr by dup2'ing a temp
// file over STDERR_FILENO because tash::io::emit writes directly with
// write(2) — gtest's CaptureStderr only hooks std::cerr.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "tash/core/parser.h"
#include "tash/util/parse_error.h"

namespace {

// RAII redirect for STDERR_FILENO → tmpfile. Mirrors the pattern in
// test_builtins_help.cpp / test_llm_diagnostics.cpp.
class StderrCapture {
public:
    StderrCapture() {
        char tmpl[] = "/tmp/tash_parse_err_XXXXXX";
        fd_ = ::mkstemp(tmpl);
        if (fd_ < 0) std::abort();
        path_ = tmpl;
        saved_ = ::dup(STDERR_FILENO);
        ::dup2(fd_, STDERR_FILENO);
    }
    ~StderrCapture() {
        if (saved_ >= 0) { ::dup2(saved_, STDERR_FILENO); ::close(saved_); }
        if (fd_ >= 0) ::close(fd_);
        ::unlink(path_.c_str());
    }
    std::string read_all() {
        ::fsync(fd_);
        ::lseek(fd_, 0, SEEK_SET);
        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd_, buf, sizeof(buf))) > 0) {
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }
private:
    int fd_ = -1;
    int saved_ = -1;
    std::string path_;
};

// Shorthand: run `fn()` with stderr redirected, return what was printed.
template <class F> std::string capture(F &&fn) {
    StderrCapture cap;
    fn();
    return cap.read_all();
}

} // namespace

// ── emit_parse_error format contract ─────────────────────────────

TEST(ParseErrorFormat, IncludesLineAndColumn) {
    std::string out = capture([] {
        tash::parse::emit_parse_error({"test message", 3, 7});
    });
    EXPECT_NE(out.find("tash:"), std::string::npos);
    EXPECT_NE(out.find("error"), std::string::npos);
    EXPECT_NE(out.find("3:7: test message"), std::string::npos) << out;
}

TEST(ParseErrorFormat, Column0IsElided) {
    // column == 0 is the convention for "unknown column"; the output
    // should still carry the line number but omit the column.
    std::string out = capture([] {
        tash::parse::emit_parse_error({"eof message", 1, 0});
    });
    EXPECT_NE(out.find("1: eof message"), std::string::npos) << out;
    // Must NOT contain "1:0:" — that would imply a real column-zero.
    EXPECT_EQ(out.find("1:0:"), std::string::npos) << out;
}

// ── offset_to_line_col ───────────────────────────────────────────

TEST(OffsetToLineCol, SingleLineIsColumnOneBased) {
    size_t ln = 0, col = 0;
    tash::parse::offset_to_line_col("echo hi", 5, ln, col);
    EXPECT_EQ(ln, 1u);
    EXPECT_EQ(col, 6u); // 1-based: 'h' sits at column 6
}

TEST(OffsetToLineCol, NewlineAdvancesLineResetsColumn) {
    size_t ln = 0, col = 0;
    // "a\nbc" → offset 3 points at 'c', which is line 2 column 2.
    tash::parse::offset_to_line_col("a\nbc", 3, ln, col);
    EXPECT_EQ(ln, 2u);
    EXPECT_EQ(col, 2u);
}

TEST(OffsetToLineCol, OffsetPastEndClampsToEnd) {
    size_t ln = 0, col = 0;
    tash::parse::offset_to_line_col("abc", 999, ln, col);
    EXPECT_EQ(ln, 1u);
    EXPECT_EQ(col, 4u); // 1-based column just past last char
}

// ── expand_variables: unterminated ${ ───────────────────────────

TEST(ParserErrors, UnmatchedBraceInVariableExpansion) {
    std::string out = capture([] {
        (void)expand_variables("before ${FOO_no_close after", 0);
    });
    EXPECT_NE(out.find("unmatched '${'"), std::string::npos) << out;
    // The `$` of `${` sits at offset 7 → column 8.
    EXPECT_NE(out.find("1:8:"), std::string::npos) << out;
}

// ── expand_command_substitution: unterminated $( ────────────────

TEST(ParserErrors, UnmatchedParenInCommandSubstitution) {
    ShellState state;
    std::string out = capture([&] {
        (void)expand_command_substitution("echo $(ls", state);
    });
    EXPECT_NE(out.find("unmatched '$('"), std::string::npos) << out;
    // `$` of `$(` is at offset 5 → column 6.
    EXPECT_NE(out.find("1:6:"), std::string::npos) << out;
}

// ── parse_redirections: missing filename targets ────────────────

TEST(ParserErrors, MissingFilenameForRedirectStdout) {
    std::string out = capture([] {
        (void)parse_redirections("echo hi >");
    });
    EXPECT_NE(out.find("missing filename for '>'"), std::string::npos) << out;
    // `>` sits at offset 8 → column 9.
    EXPECT_NE(out.find("1:9:"), std::string::npos) << out;
}

TEST(ParserErrors, MissingFilenameForAppend) {
    std::string out = capture([] {
        (void)parse_redirections("echo hi >>");
    });
    EXPECT_NE(out.find("missing filename for '>>'"), std::string::npos) << out;
    EXPECT_NE(out.find("1:9:"), std::string::npos) << out;
}

TEST(ParserErrors, MissingFilenameForStderrRedirect) {
    std::string out = capture([] {
        (void)parse_redirections("cmd 2>");
    });
    EXPECT_NE(out.find("missing filename for '2>'"), std::string::npos) << out;
    // `2>` starts at offset 4 → column 5.
    EXPECT_NE(out.find("1:5:"), std::string::npos) << out;
}

TEST(ParserErrors, MissingFilenameForInputRedirect) {
    std::string out = capture([] {
        (void)parse_redirections("cat <");
    });
    EXPECT_NE(out.find("missing filename for '<'"), std::string::npos) << out;
    // `<` sits at offset 4 → column 5.
    EXPECT_NE(out.find("1:5:"), std::string::npos) << out;
}

// ── parse_redirections: heredoc without delimiter ───────────────

TEST(ParserErrors, MissingDelimiterForHeredoc) {
    std::string out = capture([] {
        (void)parse_redirections("cat << ");
    });
    EXPECT_NE(out.find("missing delimiter for '<<'"), std::string::npos) << out;
    // `<<` starts at offset 4 → column 5.
    EXPECT_NE(out.find("1:5:"), std::string::npos) << out;
}

// ── parse_command_line: unmatched quotes at EOL ─────────────────

TEST(ParserErrors, UnmatchedDoubleQuote) {
    std::string out = capture([] {
        (void)parse_command_line("echo \"unterminated");
    });
    EXPECT_NE(out.find("unmatched '\"'"), std::string::npos) << out;
    // `"` is at offset 5 → column 6.
    EXPECT_NE(out.find("1:6:"), std::string::npos) << out;
}

TEST(ParserErrors, UnmatchedSingleQuote) {
    std::string out = capture([] {
        (void)parse_command_line("echo 'unterm");
    });
    EXPECT_NE(out.find("unmatched"), std::string::npos) << out;
    EXPECT_NE(out.find("1:6:"), std::string::npos) << out;
}

// ── parse_command_line: empty command after operator ────────────

TEST(ParserErrors, EmptyCommandAfterAnd) {
    std::string out = capture([] {
        (void)parse_command_line("ls &&");
    });
    EXPECT_NE(out.find("empty command after '&&'"), std::string::npos) << out;
}

TEST(ParserErrors, EmptyCommandAfterOr) {
    std::string out = capture([] {
        (void)parse_command_line("ls ||");
    });
    EXPECT_NE(out.find("empty command after '||'"), std::string::npos) << out;
}

TEST(ParserErrors, TrailingSemicolonIsNotAnError) {
    // `cmd ;` is idiomatic and should NOT produce a diagnostic — the
    // `;` is a terminator, not a binary operator. Regression guard
    // for the trailing-operator check.
    std::string out = capture([] {
        (void)parse_command_line("ls ;");
    });
    EXPECT_EQ(out.find("empty command"), std::string::npos)
        << "trailing ';' should be silent, got: " << out;
}

// ── sanity: well-formed inputs produce no diagnostics ───────────

TEST(ParserErrors, WellFormedRedirectIsSilent) {
    std::string out = capture([] {
        (void)parse_redirections("echo hi > /tmp/ok.txt");
    });
    EXPECT_TRUE(out.empty()) << "unexpected diagnostic: " << out;
}

TEST(ParserErrors, WellFormedVariableExpansionIsSilent) {
    ::setenv("TASH_PARSE_ERR_PROBE", "x", 1);
    std::string out = capture([] {
        (void)expand_variables("${TASH_PARSE_ERR_PROBE}", 0);
    });
    ::unsetenv("TASH_PARSE_ERR_PROBE");
    EXPECT_TRUE(out.empty()) << "unexpected diagnostic: " << out;
}

TEST(ParserErrors, BalancedCommandLineIsSilent) {
    std::string out = capture([] {
        (void)parse_command_line("echo 'ok' && echo \"done\"");
    });
    EXPECT_TRUE(out.empty()) << "unexpected diagnostic: " << out;
}
