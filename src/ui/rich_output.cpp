#include "tash/ui/rich_output.h"

#include <algorithm>
#include <sstream>
#include <cctype>

namespace tash {
namespace ui {

// ── Helpers ──────────────────────────────────────────────────

namespace {

/// Split a string into lines (preserving empty trailing line only when
/// the input ends with '\n').
std::vector<std::string> split_lines(const std::string &s) {
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while (start <= s.size()) {
        auto pos = s.find('\n', start);
        if (pos == std::string::npos) {
            lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return lines;
}

/// Tokenize a line by splitting on runs of whitespace.
std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    std::string::size_type i = 0;
    while (i < line.size()) {
        // skip whitespace
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        if (i >= line.size()) break;
        std::string::size_type start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            ++i;
        tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

/// Detect column start positions from a header line.
/// A column starts at each transition from whitespace to non-whitespace,
/// including position 0 if the line starts with a non-space character.
std::vector<std::string::size_type> detect_column_positions(
        const std::string &header) {
    std::vector<std::string::size_type> positions;
    if (header.empty()) return positions;

    bool in_space = true;
    for (std::string::size_type i = 0; i < header.size(); ++i) {
        bool is_space = (header[i] == ' ' || header[i] == '\t');
        if (in_space && !is_space) {
            positions.push_back(i);
        }
        in_space = is_space;
    }
    return positions;
}

/// Split a data line into fields using pre-detected column positions.
std::vector<std::string> split_by_columns(
        const std::string &line,
        const std::vector<std::string::size_type> &positions) {
    std::vector<std::string> fields;
    for (size_t c = 0; c < positions.size(); ++c) {
        std::string::size_type start = positions[c];
        std::string::size_type end;
        if (c + 1 < positions.size()) {
            end = positions[c + 1];
        } else {
            end = line.size();
        }
        if (start >= line.size()) {
            fields.push_back("");
            continue;
        }
        if (end > line.size()) end = line.size();
        std::string field = line.substr(start, end - start);
        // Trim trailing whitespace
        std::string::size_type last = field.find_last_not_of(" \t");
        if (last != std::string::npos) {
            field = field.substr(0, last + 1);
        } else {
            field = "";
        }
        fields.push_back(field);
    }
    return fields;
}

/// Return a string of n copies of ch.
std::string repeat_char(char ch, size_t n) {
    return std::string(n, ch);
}

/// UTF-8 aware: return the byte sequence for the box-drawing character.
/// We just use string literals directly -- the compiler handles UTF-8.

} // anonymous namespace

// ── URL Detection ────────────────────────────────────────────

bool is_url(const std::string &token) {
    // Must start with http:// or https:// and have content after the scheme
    if (token.size() > 7 && token.substr(0, 7) == "http://") {
        return token.size() > 7;  // must have something after "http://"
    }
    if (token.size() > 8 && token.substr(0, 8) == "https://") {
        return token.size() > 8;  // must have something after "https://"
    }
    return false;
}

/// Internal: find URL boundaries in text.  A URL is a contiguous run of
/// characters starting with http:// or https:// up to the next whitespace
/// or one of < > ".
static std::string::size_type find_url_start(const std::string &text,
                                              std::string::size_type from) {
    while (from < text.size()) {
        auto h = text.find("http", from);
        if (h == std::string::npos) return std::string::npos;

        // Check for http:// or https://
        if (text.substr(h, 7) == "http://" ||
            text.substr(h, 8) == "https://") {
            return h;
        }
        from = h + 1;
    }
    return std::string::npos;
}

static std::string::size_type find_url_end(const std::string &text,
                                            std::string::size_type start) {
    // Determine where the scheme ends so we can check content after it
    std::string::size_type scheme_end;
    if (text.substr(start, 8) == "https://") {
        scheme_end = start + 8;
    } else {
        scheme_end = start + 7;
    }

    // Must have at least one character after scheme
    if (scheme_end >= text.size()) return std::string::npos;

    static const std::string stop_chars = " \t\n\r<>\"";
    std::string::size_type pos = scheme_end;
    while (pos < text.size() && stop_chars.find(text[pos]) == std::string::npos) {
        ++pos;
    }

    // The URL must extend beyond the scheme
    if (pos <= scheme_end) return std::string::npos;

    return pos;
}

std::string linkify_urls(const std::string &text) {
    std::string result;
    std::string::size_type pos = 0;

    while (pos < text.size()) {
        auto url_start = find_url_start(text, pos);
        if (url_start == std::string::npos) {
            result.append(text, pos, text.size() - pos);
            break;
        }

        // Append text before the URL
        result.append(text, pos, url_start - pos);

        auto url_end = find_url_end(text, url_start);
        if (url_end == std::string::npos) {
            // Not a valid URL after all -- include the "http" prefix and move on
            result.push_back(text[url_start]);
            pos = url_start + 1;
            continue;
        }

        std::string url = text.substr(url_start, url_end - url_start);

        // Only wrap if the extracted token actually looks like a URL
        if (is_url(url)) {
            // OSC 8 hyperlink:  \033]8;;URL\033\\  TEXT  \033]8;;\033\\.
            result += "\033]8;;";
            result += url;
            result += "\033\\";
            result += url;
            result += "\033]8;;\033\\";
        } else {
            result += url;
        }

        pos = url_end;
    }

    return result;
}

// ── Table Auto-Detection ─────────────────────────────────────

bool looks_like_table(const std::string &output) {
    auto lines = split_lines(output);

    // Remove empty trailing lines
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();

    // Need at least 3 lines (header + 2 data rows)
    if (lines.size() < 3) return false;

    // Header must have 2+ tokens
    auto header_tokens = tokenize(lines[0]);
    if (header_tokens.size() < 2) return false;

    size_t expected_cols = header_tokens.size();

    // Count how many subsequent lines have the same column count
    int matching_lines = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        auto tokens = tokenize(lines[i]);
        if (tokens.size() == expected_cols) {
            ++matching_lines;
        }
    }

    return matching_lines >= 2;
}

TableData parse_table_output(const std::string &output) {
    TableData table;
    auto lines = split_lines(output);

    // Remove empty trailing lines
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();

    if (lines.empty()) return table;

    // Detect column positions from header
    auto col_positions = detect_column_positions(lines[0]);
    if (col_positions.empty()) return table;

    // Extract headers
    table.headers = split_by_columns(lines[0], col_positions);

    // Extract rows
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        auto fields = split_by_columns(lines[i], col_positions);
        table.rows.push_back(fields);
    }

    return table;
}

std::string render_table(const TableData &table) {
    if (table.headers.empty() && table.rows.empty()) return "";

    size_t num_cols = table.headers.size();
    for (const auto &row : table.rows) {
        if (row.size() > num_cols) num_cols = row.size();
    }

    if (num_cols == 0) return "";

    // Compute column widths
    std::vector<size_t> widths(num_cols, 0);
    for (size_t c = 0; c < table.headers.size(); ++c) {
        widths[c] = std::max(widths[c], table.headers[c].size());
    }
    for (const auto &row : table.rows) {
        for (size_t c = 0; c < row.size(); ++c) {
            widths[c] = std::max(widths[c], row[c].size());
        }
    }

    // Ensure minimum column width of 1
    for (auto &w : widths) {
        if (w == 0) w = 1;
    }

    // Helper lambdas for box-drawing lines
    // Using UTF-8 box-drawing characters directly.

    // Build horizontal border: left + repeated ─ + junction + ... + right
    auto hline = [&](const std::string &left,
                     const std::string &mid,
                     const std::string &right) -> std::string {
        std::string line = left;
        for (size_t c = 0; c < num_cols; ++c) {
            // Each cell has 1 space padding on each side => width + 2
            for (size_t i = 0; i < widths[c] + 2; ++i)
                line += "\xe2\x94\x80"; // ─  (U+2500)
            if (c + 1 < num_cols)
                line += mid;
        }
        line += right;
        line += "\n";
        return line;
    };

    // Build data row:  │ val (padded) │ val │ ...
    auto data_row = [&](const std::vector<std::string> &cells) -> std::string {
        std::string line = "\xe2\x94\x82"; // │
        for (size_t c = 0; c < num_cols; ++c) {
            std::string cell;
            if (c < cells.size()) cell = cells[c];
            line += " ";
            line += cell;
            line += repeat_char(' ', widths[c] - cell.size());
            line += " ";
            line += "\xe2\x94\x82"; // │
        }
        line += "\n";
        return line;
    };

    std::string out;

    // ┌──────┬───────┐
    out += hline("\xe2\x94\x8c", // ┌
                 "\xe2\x94\xac", // ┬
                 "\xe2\x94\x90"); // ┐

    // │ header cells │
    if (!table.headers.empty()) {
        out += data_row(table.headers);

        // ├──────┼───────┤
        out += hline("\xe2\x94\x9c", // ├
                     "\xe2\x94\xbc", // ┼
                     "\xe2\x94\xa4"); // ┤
    }

    // Data rows
    for (const auto &row : table.rows) {
        out += data_row(row);
    }

    // └──────┴───────┘
    out += hline("\xe2\x94\x94", // └
                 "\xe2\x94\xb4", // ┴
                 "\xe2\x94\x98"); // ┘

    return out;
}

// ── Output Export ────────────────────────────────────────────

std::string export_as_markdown(const std::string &command,
                                const std::string &output) {
    std::string md;
    md += "```\n";
    md += "$ " + command + "\n";
    md += output;
    if (!output.empty() && output.back() != '\n')
        md += "\n";
    md += "```\n";
    return md;
}

} // namespace ui
} // namespace tash
