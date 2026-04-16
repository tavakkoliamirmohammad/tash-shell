#include "test_helpers.h"

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
