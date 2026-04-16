#include "tash/ui/fuzzy_finder.h"

#include <algorithm>
#include <cctype>
#include <climits>

namespace tash {

// ── Internal helpers ─────────────────────────────────────────────

namespace {

/// Case-insensitive char comparison.
inline bool chars_equal_ci(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
}

/// Return true if \p c is a word-boundary separator.
inline bool is_separator(char c) {
    return c == ' ' || c == '-' || c == '_' || c == '/';
}

/// Return true if position \p pos in \p s is a word-boundary start.
inline bool is_boundary(const std::string &s, size_t pos) {
    if (pos == 0) return true;
    return is_separator(s[pos - 1]);
}

// ── Scoring constants ────────────────────────────────────────────

static const int SCORE_CONSECUTIVE_BONUS = 5;  // bonus per consecutive char
static const int SCORE_BOUNDARY_BONUS    = 5;  // bonus for match at word start
static const int SCORE_START_BONUS       = 7;  // extra bonus for match at pos 0
static const int SCORE_GAP_PENALTY       = -1; // per char gap between matches
static const int SCORE_BASE_MATCH        = 1;  // base per matched char

// ── Recursive best-path scorer ───────────────────────────────────
//
// For each query character we try every possible matching position in the
// candidate and pick the path that yields the highest total score.  To keep
// the cost bounded we use memoisation keyed on (query index, candidate
// index).

struct ScorerState {
    const std::string &query;
    const std::string &candidate;
    // memo[qi * cand_len + ci] – INT_MIN means "not computed yet"
    std::vector<int> memo;
    size_t cand_len;

    ScorerState(const std::string &q, const std::string &c)
        : query(q), candidate(c), cand_len(c.size()) {
        memo.assign(q.size() * cand_len, INT_MIN);
    }

    /// Return the best score obtainable by matching query[qi..] against
    /// candidate[ci..], given that the *previous* match was at position
    /// \p prev_ci (-1 if this is the first character).
    int solve(size_t qi, size_t ci, int prev_ci);
};

int ScorerState::solve(size_t qi, size_t ci, int prev_ci) {
    if (qi == query.size()) return 0;                        // all matched
    if (ci >= cand_len)    return INT_MIN;                    // ran out of room

    // Remaining candidate chars must be >= remaining query chars.
    if (cand_len - ci < query.size() - qi) return INT_MIN;

    // We iterate over candidate positions, trying every valid position for
    // this query char and taking the path that yields the maximum total.

    int best = INT_MIN; // INT_MIN means "no valid path found"
    for (size_t j = ci; j < cand_len; j++) {
        // Remaining candidate chars must be >= remaining query chars.
        if (cand_len - j < query.size() - qi) break;

        if (!chars_equal_ci(query[qi], candidate[j])) continue;

        // Compute the score contribution from matching query[qi] at
        // candidate[j].
        int local = SCORE_BASE_MATCH;

        bool consecutive = (prev_ci >= 0 &&
                            static_cast<size_t>(prev_ci) == j - 1);
        bool at_boundary = is_boundary(candidate, j);

        // Consecutive bonus: previous match was at j-1.
        if (consecutive) {
            local += SCORE_CONSECUTIVE_BONUS;
        }

        // Boundary bonus.
        if (at_boundary) {
            local += SCORE_BOUNDARY_BONUS;
            if (j == 0) local += SCORE_START_BONUS;
        }

        // Gap penalty: chars skipped between prev_ci and j.
        // Waived when the match lands on a word boundary (the user is
        // intentionally jumping to the next word).
        if (prev_ci >= 0 && !at_boundary && !consecutive) {
            int gap = static_cast<int>(j) - prev_ci - 1;
            if (gap > 0) local += gap * SCORE_GAP_PENALTY;
        }

        int rest = solve(qi + 1, j + 1, static_cast<int>(j));
        if (rest == INT_MIN) continue; // subsequent chars couldn't be matched

        int total = local + rest;
        if (total > best) best = total;
    }
    return best;
}

} // anonymous namespace

// ── Public API ───────────────────────────────────────────────────

int fuzzy_score(const std::string &query, const std::string &candidate) {
    if (query.empty()) return 1;        // empty query matches everything
    if (candidate.empty()) return 0;    // non-empty query vs empty candidate
    if (query.size() > candidate.size()) return 0;

    ScorerState state(query, candidate);
    int result = state.solve(0, 0, -1);
    if (result == INT_MIN) return 0;       // no valid subsequence match
    // A valid subsequence was found; guarantee at least score 1.
    return result < 1 ? 1 : result;
}

std::vector<FuzzyResult> fuzzy_filter(
    const std::string &query,
    const std::vector<std::string> &candidates,
    int max_results)
{
    std::vector<FuzzyResult> scored;
    scored.reserve(candidates.size());

    for (const auto &c : candidates) {
        int s = fuzzy_score(query, c);
        if (s > 0) {
            scored.push_back(FuzzyResult(c, s));
        }
    }

    // Sort: higher score first; on tie, shorter candidate first.
    std::sort(scored.begin(), scored.end(),
              [](const FuzzyResult &a, const FuzzyResult &b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.text.size() < b.text.size();
              });

    if (max_results > 0 && static_cast<int>(scored.size()) > max_results) {
        scored.resize(static_cast<size_t>(max_results));
    }

    return scored;
}

} // namespace tash
