#ifndef TASH_UTIL_PARSE_ERROR_H
#define TASH_UTIL_PARSE_ERROR_H

// Lightweight parse-error reporting. Previously parser/executor emitted
// bare "tash: unmatched '('" strings with no indication of where in the
// input the problem was. This header provides:
//
//   * ParseError      — {message, line, column}; callers populate from
//                       context and pass to emit_parse_error().
//   * emit_parse_error — routes through tash::io::error with the
//                       "tash:LINE:COL: error: MSG" format shells like
//                       bash/zsh adopted years ago.
//   * offset_to_line_col — convert a byte offset in the input to a
//                          1-based (line, column) pair.
//
// Deep-review finding O7.3.

#include <cstddef>
#include <string>
#include <string_view>

namespace tash::parse {

struct ParseError {
    std::string message;
    size_t line = 1;
    size_t column = 0;  // 0 means "unknown column"; otherwise 1-based
};

// Compute (line, column) from a byte offset within `input`. Lines and
// columns are 1-based to match every editor/shell users know. If
// `offset` is past the end it clamps to the final position.
void offset_to_line_col(std::string_view input, size_t offset,
                        size_t &line_out, size_t &column_out);

// Emit `err` via tash::io::error with the canonical
// "tash:LINE:COL: error: MSG" prefix. Column 0 is elided.
void emit_parse_error(const ParseError &err);

} // namespace tash::parse

#endif // TASH_UTIL_PARSE_ERROR_H
