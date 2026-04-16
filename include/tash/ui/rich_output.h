#ifndef TASH_UI_RICH_OUTPUT_H
#define TASH_UI_RICH_OUTPUT_H

#include <string>
#include <vector>

namespace tash {
namespace ui {

// ── URL Detection and OSC 8 Wrapping ─────────────────────────

/// Returns true if token looks like a URL (starts with http:// or https://
/// and has content after the scheme).
bool is_url(const std::string &token);

/// Find URLs matching https?://[^\s<>"]+  in the text and wrap each with
/// OSC 8 hyperlink escape sequences:
///   \033]8;;URL\033\\TEXT\033]8;;\033\\
/// Non-URL text is returned unchanged.
std::string linkify_urls(const std::string &text);

// ── Table Auto-Detection ─────────────────────────────────────

/// Structured representation of tabular data.
struct TableData {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

/// Heuristic check: does the output look like columnar / tabular text?
///  1. Split into lines
///  2. First line has 2+ whitespace-separated tokens (potential header)
///  3. At least 2 subsequent lines have the same number of "columns"
bool looks_like_table(const std::string &output);

/// Parse columnar output into structured data.
/// Uses column positions from the header line to split data lines.
TableData parse_table_output(const std::string &output);

/// Render a TableData with Unicode box-drawing characters.
std::string render_table(const TableData &table);

// ── Output Export ────────────────────────────────────────────

/// Export command + output as a Markdown fenced code block.
std::string export_as_markdown(const std::string &command,
                                const std::string &output);

} // namespace ui
} // namespace tash

#endif // TASH_UI_RICH_OUTPUT_H
