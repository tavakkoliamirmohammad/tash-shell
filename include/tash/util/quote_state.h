#ifndef TASH_UTIL_QUOTE_STATE_H
#define TASH_UTIL_QUOTE_STATE_H

// Quote-state tracker shared by every scanner that needs to know whether
// the current character is inside '...' or "...". Parser operator-split,
// tokenizer, comment stripper, structured-pipe tokenizer, executor
// subshell matcher, highlighter and fish argv split all used to carry
// near-identical copies of the same two-bool toggle. Consolidating them
// here means one canonical definition of the rule:
//
//   * `'` toggles single-quote state when not inside "...".
//   * `"` toggles double-quote state when not inside '...'.
//   * `any_active()` — true when the next char is inside quotes.
//
// Kept header-only + branch-simple so every caller can inline it and the
// compiler eliminates the call frame. No heredoc or backslash handling
// lives here on purpose: those rules differ between callers (tokenize
// keeps the backslash, the variable expander consumes it) and belong in
// the caller's loop.

namespace tash::util {

struct QuoteState {
    bool in_single = false;
    bool in_double = false;

    // Apply the quote toggle for `c` if it is a quote-boundary character
    // not suppressed by the other quote state. Returns true when the
    // character participated in quote bookkeeping so callers can `continue`
    // past it (or not, if they want the quote char in their output).
    bool consume(char c) {
        if (c == '\'' && !in_double) { in_single = !in_single; return true; }
        if (c == '"'  && !in_single) { in_double = !in_double; return true; }
        return false;
    }

    bool any_active() const { return in_single || in_double; }
};

} // namespace tash::util

#endif // TASH_UTIL_QUOTE_STATE_H
