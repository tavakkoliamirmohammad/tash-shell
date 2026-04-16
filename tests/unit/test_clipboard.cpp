#include <gtest/gtest.h>
#include "tash/ui/clipboard.h"

// ═══════════════════════════════════════════════════════════════
// Base64 encoding tests
// ═══════════════════════════════════════════════════════════════

TEST(Base64, EncodeEmpty) {
    EXPECT_EQ(base64_encode(""), "");
}

TEST(Base64, EncodeSimple) {
    EXPECT_EQ(base64_encode("hello"), "aGVsbG8=");
}

TEST(Base64, EncodeWithPadding) {
    EXPECT_EQ(base64_encode("a"), "YQ==");
}

TEST(Base64, EncodeTwoPadding) {
    EXPECT_EQ(base64_encode("ab"), "YWI=");
}

TEST(Base64, EncodeLong) {
    EXPECT_EQ(base64_encode("The quick brown fox jumps over the lazy dog"),
              "VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw==");
}

// ═══════════════════════════════════════════════════════════════
// OSC 52 encoding tests
// ═══════════════════════════════════════════════════════════════

TEST(Osc52, EncodeCorrect) {
    std::string result = osc52_encode("hello");
    EXPECT_EQ(result, std::string("\033]52;c;aGVsbG8=\a"));
}

TEST(Osc52, EncodeEmpty) {
    std::string result = osc52_encode("");
    EXPECT_EQ(result, std::string("\033]52;c;\a"));
}

// ═══════════════════════════════════════════════════════════════
// is_multiline tests
// ═══════════════════════════════════════════════════════════════

TEST(IsMultiline, SingleLine) {
    EXPECT_FALSE(is_multiline("single line"));
}

TEST(IsMultiline, MultiLine) {
    EXPECT_TRUE(is_multiline("line1\nline2"));
}

TEST(IsMultiline, Empty) {
    EXPECT_FALSE(is_multiline(""));
}
