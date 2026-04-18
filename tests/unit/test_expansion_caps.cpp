// Tests for expand_variables() refusing to balloon past
// TASH_MAX_EXPANSION_BYTES. Declares the helper from core.h to avoid
// pulling in the rest of the shell; the parser TU is added to the
// test's source list.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "tash/core/parser.h"
#include "tash/util/limits.h"

TEST(ExpansionCaps, NormalExpansionUnaffected) {
    ::setenv("TASH_CAP_TEST_VAR", "hello", 1);
    std::string out = expand_variables("greet=$TASH_CAP_TEST_VAR", 0);
    EXPECT_EQ(out, "greet=hello");
    ::unsetenv("TASH_CAP_TEST_VAR");
}

TEST(ExpansionCaps, OversizeVariableBailsCleanly) {
    // Build a variable whose value is 2x the cap, then reference it.
    // The helper must refuse and return an empty string.
    std::string huge(tash::util::TASH_MAX_EXPANSION_BYTES + 10, 'x');
    ::setenv("TASH_CAP_TEST_HUGE", huge.c_str(), 1);
    std::string out = expand_variables("before $TASH_CAP_TEST_HUGE after", 0);
    EXPECT_TRUE(out.empty())
        << "cap-exceeding expansion must return empty, got size="
        << out.size();
    ::unsetenv("TASH_CAP_TEST_HUGE");
}

TEST(ExpansionCaps, RepeatedReferenceEventuallyCaps) {
    // Each expansion of the 600 KiB variable is under the cap, but
    // three consecutive references blow past it. The cap check fires
    // cumulatively on the result, not per-reference, so the helper
    // aborts mid-expansion.
    std::string half(tash::util::TASH_MAX_EXPANSION_BYTES - 1000, 'y');
    ::setenv("TASH_CAP_TEST_HALF", half.c_str(), 1);
    std::string out = expand_variables(
        "$TASH_CAP_TEST_HALF$TASH_CAP_TEST_HALF$TASH_CAP_TEST_HALF", 0);
    EXPECT_TRUE(out.empty());
    ::unsetenv("TASH_CAP_TEST_HALF");
}
