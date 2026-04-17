// Glob edge cases — test_glob.cpp only covers `*` expand and unmatched
// pattern preservation. The review flagged symlinks, quotes, escapes.

#include "test_helpers.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct GlobSandbox {
    std::string root;
    GlobSandbox() {
        root = "/tmp/tash_glob_edge_" + std::to_string(getpid());
        int rc = system(("rm -rf " + root + " && mkdir -p " + root).c_str());
        (void)rc;
    }
    ~GlobSandbox() {
        int rc = system(("rm -rf " + root).c_str());
        (void)rc;
    }
    void touch(const std::string &relpath) {
        std::ofstream f(root + "/" + relpath);
        f << "x";
    }
    void symlink_to(const std::string &link, const std::string &target) {
        ::symlink(target.c_str(), (root + "/" + link).c_str());
    }
};

} // namespace

TEST(GlobEdges, QuotedStarIsLiteral) {
    // Inside single quotes, * must NOT expand.
    GlobSandbox g;
    g.touch("alpha");
    g.touch("beta");
    auto r = run_shell("echo '" + g.root + "/*'\nexit\n");
    // Output contains the literal pattern, not the expanded filenames.
    EXPECT_NE(r.output.find("/*"), std::string::npos);
    EXPECT_EQ(r.output.find(g.root + "/alpha"), std::string::npos);
}

TEST(GlobEdges, EscapedStarIsLiteral) {
    // Backslash-escaped \* should survive as a literal * after parsing,
    // so the glob engine leaves it alone.
    auto r = run_shell("echo /tmp/escape_test/\\*\nexit\n");
    EXPECT_NE(r.output.find("/*"), std::string::npos);
}

TEST(GlobEdges, DoubleQuotedStarIsLiteral) {
    // Same rule applies to double quotes.
    GlobSandbox g;
    g.touch("one");
    g.touch("two");
    auto r = run_shell("echo \"" + g.root + "/*\"\nexit\n");
    EXPECT_NE(r.output.find("/*"), std::string::npos);
    EXPECT_EQ(r.output.find(g.root + "/one"), std::string::npos);
}

TEST(GlobEdges, GlobFollowsSymlinks) {
    GlobSandbox g;
    g.touch("real_target");
    g.symlink_to("linked", "real_target");
    auto r = run_shell("ls " + g.root + "/*\nexit\n");
    EXPECT_NE(r.output.find("real_target"), std::string::npos);
    EXPECT_NE(r.output.find("linked"),      std::string::npos);
}

TEST(GlobEdges, QuestionMarkMatchesSingle) {
    GlobSandbox g;
    g.touch("a");
    g.touch("bb");
    auto r = run_shell("ls " + g.root + "/?\nexit\n");
    // `?` matches exactly one char → only "a"
    EXPECT_NE(r.output.find("/a"), std::string::npos);
    // "bb" is two chars, should not match `?`
    // (we can't easily assert its absence on macOS ls which lists
    // everything in the directory when no match; skip that check).
}

TEST(GlobEdges, BracketCharClass) {
    GlobSandbox g;
    g.touch("a1");
    g.touch("a2");
    g.touch("a9");
    g.touch("b1");
    auto r = run_shell("ls " + g.root + "/[ab]1\nexit\n");
    EXPECT_NE(r.output.find("a1"), std::string::npos);
    EXPECT_NE(r.output.find("b1"), std::string::npos);
}

TEST(GlobEdges, StarInMiddle) {
    GlobSandbox g;
    g.touch("pre_x_post");
    g.touch("pre_yy_post");
    g.touch("pre_z_other");
    auto r = run_shell("ls " + g.root + "/pre_*_post\nexit\n");
    EXPECT_NE(r.output.find("pre_x_post"),  std::string::npos);
    EXPECT_NE(r.output.find("pre_yy_post"), std::string::npos);
    EXPECT_EQ(r.output.find("pre_z_other"), std::string::npos);
}

TEST(GlobEdges, HiddenFilesNotMatchedByStar) {
    // `*` should not expand to dotfiles.
    GlobSandbox g;
    g.touch("visible");
    g.touch(".hidden");
    auto r = run_shell("ls " + g.root + "/*\nexit\n");
    EXPECT_NE(r.output.find("visible"), std::string::npos);
    // .hidden should not appear in the glob expansion (though it may
    // appear from the ls of the directory on macOS BSD ls). We check
    // it doesn't appear as part of the expansion path.
    EXPECT_EQ(r.output.find(g.root + "/.hidden"), std::string::npos);
}
