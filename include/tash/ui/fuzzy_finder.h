#ifndef TASH_UI_FUZZY_FINDER_H
#define TASH_UI_FUZZY_FINDER_H

#include <string>
#include <vector>

namespace tash {

// ── Fuzzy match result ───────────────────────────────────────────

struct FuzzyResult {
    std::string text;
    int score;

    FuzzyResult() : score(0) {}
    FuzzyResult(const std::string &t, int s) : text(t), score(s) {}
};

// ── Fuzzy matching scorer ────────────────────────────────────────

/// Score a query against a candidate string using fuzzy matching.
///
/// Returns score >= 0.  Higher = better match.  0 = no match.
///
/// Scoring rules:
///  - Each query char must appear in candidate (in order, not consecutive)
///  - Bonus for consecutive matches (+3 per consecutive char after first)
///  - Bonus for matches at word boundaries (after ' ', '-', '_', '/') (+5)
///  - Bonus for match at string start (+7)
///  - Penalty for gaps between matches (-1 per gap char)
///  - Returns 0 if any query char is not found in candidate
int fuzzy_score(const std::string &query, const std::string &candidate);

/// Filter a list of candidates against a query and return the top matches,
/// sorted by score descending (best first).
///
/// - Empty query matches every candidate (score 1 each).
/// - max_results limits the returned set (default 20).
std::vector<FuzzyResult> fuzzy_filter(
    const std::string &query,
    const std::vector<std::string> &candidates,
    int max_results = 20);

} // namespace tash

#endif // TASH_UI_FUZZY_FINDER_H
