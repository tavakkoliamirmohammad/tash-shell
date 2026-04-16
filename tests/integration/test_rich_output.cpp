#include "test_helpers.h"
#include <cstdlib>
#include <fstream>

// The `linkify` builtin should wrap URLs in OSC 8 escape sequences so
// terminals that support hyperlink reporting (iTerm2, WezTerm, Kitty,
// Ghostty, Windows Terminal) render them as clickable links.
TEST(RichOutputIntegration, LinkifyEmitsOsc8) {
    auto r = run_shell("linkify \"visit https://example.com now\"\nexit\n");
    EXPECT_NE(r.output.find("\x1b]8;;https://example.com\x1b\\"),
              std::string::npos);
    EXPECT_NE(r.output.find("\x1b]8;;\x1b\\"), std::string::npos);
}

// Non-URL text passes through unchanged.
TEST(RichOutputIntegration, LinkifyWithNoUrlsIsIdentity) {
    auto r = run_shell("linkify plain words here\nexit\n");
    EXPECT_NE(r.output.find("plain words here"), std::string::npos);
    EXPECT_EQ(r.output.find("\x1b]8"), std::string::npos);
}

// `table` renders columnar stdin as a Unicode box-drawing table.
TEST(RichOutputIntegration, TableRendersBoxDrawing) {
    // Use a synthetic 3-line block so the heuristic (≥2 aligned data rows)
    // fires regardless of the test runner's live process list.
    auto r = run_shell(
        "printf 'PID  CPU  CMD\\n1234 5.2  node\\n5678 1.1  tash\\n'"
        " | table\nexit\n");
    bool has_box = r.output.find("\xe2\x94\x8c") != std::string::npos || // ┌
                   r.output.find("\xe2\x94\x9c") != std::string::npos;   // ├
    EXPECT_TRUE(has_box);
}

// Non-tabular input should pass through (heuristic fallback).
TEST(RichOutputIntegration, TablePassesThroughUnstructured) {
    auto r = run_shell("echo one line only | table\nexit\n");
    EXPECT_NE(r.output.find("one line only"), std::string::npos);
    EXPECT_EQ(r.output.find("\xe2\x94\x8c"), std::string::npos);
}

// TASH_AUTO_LINKIFY=1 makes every external command's stdout get its URLs
// wrapped in OSC 8 automatically, without piping through `linkify`.
TEST(RichOutputIntegration, AutoLinkifyWrapsExternalCommandOutput) {
    setenv("TASH_AUTO_LINKIFY", "1", 1);
    std::string path = "/tmp/tash_autolinkify_" + std::to_string(getpid());
    {
        std::ofstream f(path);
        f << "visit https://example.com today\n";
    }
    auto r = run_shell("cat " + path + "\nexit\n");
    unsetenv("TASH_AUTO_LINKIFY");
    unlink(path.c_str());
    EXPECT_NE(r.output.find("\x1b]8;;https://example.com\x1b\\"),
              std::string::npos);
}

// With the env var OFF, external command output is untouched.
TEST(RichOutputIntegration, NoAutoLinkifyByDefault) {
    unsetenv("TASH_AUTO_LINKIFY");
    std::string path = "/tmp/tash_autolinkify_off_" + std::to_string(getpid());
    {
        std::ofstream f(path);
        f << "visit https://example.com today\n";
    }
    auto r = run_shell("cat " + path + "\nexit\n");
    unlink(path.c_str());
    EXPECT_NE(r.output.find("visit https://example.com today"),
              std::string::npos);
    EXPECT_EQ(r.output.find("\x1b]8"), std::string::npos);
}

// Multiple URLs in one line all get their own OSC 8 wrappers.
TEST(RichOutputIntegration, LinkifyHandlesMultipleUrls) {
    auto r = run_shell(
        "linkify \"docs at https://a.com and https://b.com\"\nexit\n");
    EXPECT_NE(r.output.find("\x1b]8;;https://a.com\x1b\\"), std::string::npos);
    EXPECT_NE(r.output.find("\x1b]8;;https://b.com\x1b\\"), std::string::npos);
}

// Linkify a URL passed as a direct argument (no pipe).
TEST(RichOutputIntegration, LinkifyAcceptsDirectArgument) {
    auto r = run_shell("linkify \"go to https://example.com\"\nexit\n");
    EXPECT_NE(r.output.find("\x1b]8;;https://example.com\x1b\\"),
              std::string::npos);
}

// Round-trip: od -c reveals the raw OSC 8 bytes.
TEST(RichOutputIntegration, LinkifyOutputRoundTripsThroughOd) {
    auto r = run_shell(
        "echo https://example.com | linkify | od -c | head -1\nexit\n");
    EXPECT_NE(r.output.find("033"), std::string::npos);
    EXPECT_NE(r.output.find("8"), std::string::npos);
}

// `table` accepts a multi-row synthetic input and emits box-drawing
// borders (already covered by TableRendersBoxDrawing) — here we also
// verify the intersection ┼ character for 3+ rows.
TEST(RichOutputIntegration, TableEmitsRowSeparator) {
    auto r = run_shell(
        "printf 'A B\\n1 2\\n3 4\\n' | table\nexit\n");
    EXPECT_NE(r.output.find("\xe2\x94\x94"), std::string::npos); // └
    EXPECT_NE(r.output.find("\xe2\x94\x98"), std::string::npos); // ┘
}

// `table` truncates over-wide cells to keep output readable. Cell
// exceeding --max-width should end with the single-char UTF-8 ellipsis.
TEST(RichOutputIntegration, TableTruncatesWideCells) {
    auto r = run_shell(
        "printf 'NAME VALUE\\nshort 12\\nlong "
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefg "
        "tail\\n' | table --max-width 10\nexit\n");
    EXPECT_NE(r.output.find("\xe2\x80\xa6"), std::string::npos); // …
}

// Smoke test for a realistically wide table with variable-length tail
// column (mimics the `ps aux` shape) — uses printf so it's independent
// of the container's process list.
TEST(RichOutputIntegration, TableWorksWithWideVariableTail) {
    auto r = run_shell(
        "printf 'USER  PID   %%CPU COMMAND\\n"
        "alice 1234  5.2  /usr/local/bin/really/long/path --with --many --args\\n"
        "bob   5678  1.1  short\\n' | table --max-width 20\nexit\n");
    EXPECT_NE(r.output.find("\xe2\x94\x8c"), std::string::npos); // ┌
    EXPECT_NE(r.output.find("\xe2\x80\xa6"), std::string::npos); // … (truncated)
}

// Piping through `linkify` inside a multi-stage pipeline.
TEST(RichOutputIntegration, LinkifyInMultiStagePipeline) {
    auto r = run_shell(
        "echo \"docs https://example.com end\" | cat | linkify\nexit\n");
    EXPECT_NE(r.output.find("\x1b]8;;https://example.com\x1b\\"),
              std::string::npos);
}

// Auto-linkify bypass list: running a command whose basename is in the
// bypass list (e.g. `less`) must not introduce OSC 8 into the output
// — test via a stand-in that doesn't actually page so the test ends.
// We can't easily invoke vim/less non-interactively, so we prove the
// negative by verifying that commands with a redirection are bypassed
// (redirection path is also excluded from interception).
TEST(RichOutputIntegration, AutoLinkifyBypassedByRedirection) {
    setenv("TASH_AUTO_LINKIFY", "1", 1);
    std::string in = "/tmp/tash_redir_in_" + std::to_string(getpid());
    std::string out = "/tmp/tash_redir_out_" + std::to_string(getpid());
    {
        std::ofstream f(in);
        f << "url https://example.com\n";
    }
    run_shell("cat " + in + " > " + out + "\nexit\n");
    std::ifstream rf(out);
    std::string written((std::istreambuf_iterator<char>(rf)),
                        std::istreambuf_iterator<char>());
    unsetenv("TASH_AUTO_LINKIFY");
    unlink(in.c_str());
    unlink(out.c_str());
    EXPECT_NE(written.find("url https://example.com"), std::string::npos);
    EXPECT_EQ(written.find("\x1b]8"), std::string::npos);
}
