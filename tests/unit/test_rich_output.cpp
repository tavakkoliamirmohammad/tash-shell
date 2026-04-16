#include <gtest/gtest.h>
#include "tash/ui/rich_output.h"

using namespace tash::ui;

// ── URL detection tests ──────────────────────────────────────

TEST(RichOutput, DetectsHttpUrl) {
    std::string text = "Visit https://example.com here";
    std::string result = linkify_urls(text);
    // The URL should be wrapped with OSC 8 escapes
    EXPECT_NE(result.find("\033]8;;https://example.com\033\\"), std::string::npos);
    EXPECT_NE(result.find("https://example.com\033]8;;\033\\"), std::string::npos);
}

TEST(RichOutput, DetectsHttpsUrlWithPath) {
    std::string url = "https://example.com/path?q=1";
    EXPECT_TRUE(is_url(url));
    std::string text = "Check " + url + " now";
    std::string result = linkify_urls(text);
    EXPECT_NE(result.find("\033]8;;" + url + "\033\\"), std::string::npos);
}

TEST(RichOutput, IgnoresNonUrl) {
    std::string text = "not a url";
    std::string result = linkify_urls(text);
    EXPECT_EQ(result, text);
}

TEST(RichOutput, IgnoresPartialUrl) {
    // "http://" alone (no content after scheme) should not be linkified
    std::string text = "broken http:// link";
    std::string result = linkify_urls(text);
    // Should not contain OSC 8 escape
    EXPECT_EQ(result.find("\033]8;;"), std::string::npos);
}

TEST(RichOutput, Osc8WrapCorrect) {
    std::string text = "see https://github.com/tash end";
    std::string result = linkify_urls(text);
    // Expected: "see " + OSC8_START + URL + ST + URL + OSC8_END + " end"
    std::string expected = "see "
        "\033]8;;https://github.com/tash\033\\"
        "https://github.com/tash"
        "\033]8;;\033\\"
        " end";
    EXPECT_EQ(result, expected);
}

TEST(RichOutput, PreservesNonUrlText) {
    std::string text = "hello world, no urls here!";
    EXPECT_EQ(linkify_urls(text), text);
}

TEST(RichOutput, MultipleUrls) {
    std::string text = "A http://a.com B https://b.org C";
    std::string result = linkify_urls(text);
    // Both URLs should be wrapped
    EXPECT_NE(result.find("\033]8;;http://a.com\033\\"), std::string::npos);
    EXPECT_NE(result.find("\033]8;;https://b.org\033\\"), std::string::npos);
}

TEST(RichOutput, IsUrlTrue) {
    EXPECT_TRUE(is_url("https://github.com"));
    EXPECT_TRUE(is_url("http://example.org/path"));
}

TEST(RichOutput, IsUrlFalse) {
    EXPECT_FALSE(is_url("hello"));
    EXPECT_FALSE(is_url("ftp://files.com"));
    EXPECT_FALSE(is_url("http://"));
    EXPECT_FALSE(is_url("https://"));
    EXPECT_FALSE(is_url(""));
}

// ── Table heuristic tests ────────────────────────────────────

TEST(RichOutput, TableHeuristicDetects) {
    std::string output = "PID CMD\n1234 node\n5678 py";
    EXPECT_TRUE(looks_like_table(output));
}

TEST(RichOutput, TableHeuristicRejectsSingleLine) {
    std::string output = "just one line";
    EXPECT_FALSE(looks_like_table(output));
}

TEST(RichOutput, TableHeuristicRejectsUnaligned) {
    // Header has 2 columns, but subsequent lines have varying columns
    std::string output = "COL1 COL2\none two three\nfour";
    EXPECT_FALSE(looks_like_table(output));
}

// ── Table rendering tests ────────────────────────────────────

TEST(RichOutput, RenderTableBasic) {
    TableData table;
    table.headers = {"PID", "CMD"};
    table.rows = {{"1234", "node"}, {"5678", "py"}};

    std::string rendered = render_table(table);

    // Check for box-drawing characters (UTF-8 encoded)
    EXPECT_NE(rendered.find("\xe2\x94\x8c"), std::string::npos); // ┌
    EXPECT_NE(rendered.find("\xe2\x94\x90"), std::string::npos); // ┐
    EXPECT_NE(rendered.find("\xe2\x94\x94"), std::string::npos); // └
    EXPECT_NE(rendered.find("\xe2\x94\x98"), std::string::npos); // ┘
    EXPECT_NE(rendered.find("\xe2\x94\x82"), std::string::npos); // │
    EXPECT_NE(rendered.find("\xe2\x94\x80"), std::string::npos); // ─

    // Check that data is present
    EXPECT_NE(rendered.find("PID"), std::string::npos);
    EXPECT_NE(rendered.find("CMD"), std::string::npos);
    EXPECT_NE(rendered.find("1234"), std::string::npos);
    EXPECT_NE(rendered.find("node"), std::string::npos);
}

TEST(RichOutput, RenderTableEmpty) {
    TableData table;
    EXPECT_EQ(render_table(table), "");
}

// ── Markdown export test ─────────────────────────────────────

TEST(RichOutput, ExportMarkdown) {
    std::string md = export_as_markdown("ls -la", "total 42\nfile.txt");
    EXPECT_NE(md.find("```"), std::string::npos);
    EXPECT_NE(md.find("$ ls -la"), std::string::npos);
    EXPECT_NE(md.find("total 42"), std::string::npos);
    EXPECT_NE(md.find("file.txt"), std::string::npos);
}
