// pushd / popd / dirs chain coverage — only trivial no-arg cases were
// tested before. These exercise the full directory stack semantics.

#include "test_helpers.h"

#include <sys/stat.h>
#include <unistd.h>

namespace {

struct TempDirs {
    std::string a, b, c;
    TempDirs() {
        std::string root = "/tmp/tash_dstack_" + std::to_string(getpid());
        a = root + "/a";
        b = root + "/b";
        c = root + "/c";
        int rc = system(("rm -rf " + root + " && mkdir -p " +
                         a + " " + b + " " + c).c_str());
        (void)rc;
    }
    ~TempDirs() {
        int rc = system(("rm -rf /tmp/tash_dstack_" +
                         std::to_string(getpid())).c_str());
        (void)rc;
    }
};

} // namespace

TEST(DirStack, PushdChangesCurrentDir) {
    TempDirs d;
    auto r = run_shell("pushd " + d.a + "\npwd\nexit\n");
    EXPECT_NE(r.output.find(d.a), std::string::npos);
}

TEST(DirStack, PushdThenPopdReturnsToOrigin) {
    TempDirs d;
    auto r = run_shell(
        "cd " + d.a + "\n"
        "pushd " + d.b + "\n"
        "popd\n"
        "pwd\n"
        "exit\n");
    // After popd we should be back in d.a. The final pwd line should
    // print d.a, not d.b.
    size_t last_a = r.output.rfind(d.a);
    size_t last_b = r.output.rfind(d.b);
    ASSERT_NE(last_a, std::string::npos);
    // If d.b appeared at all, d.a should appear AFTER it (since pwd
    // runs after popd).
    if (last_b != std::string::npos) {
        EXPECT_GT(last_a, last_b);
    }
}

TEST(DirStack, DirsShowsCurrentStack) {
    TempDirs d;
    auto r = run_shell(
        "cd " + d.a + "\n"
        "pushd " + d.b + "\n"
        "dirs\n"
        "exit\n");
    // After pushd, stack has [d.b, d.a] and dirs prints both.
    EXPECT_NE(r.output.find(d.a), std::string::npos);
    EXPECT_NE(r.output.find(d.b), std::string::npos);
}

TEST(DirStack, MultiplePushdPopd) {
    TempDirs d;
    auto r = run_shell(
        "cd " + d.a + "\n"
        "pushd " + d.b + "\n"
        "pushd " + d.c + "\n"
        "popd\n"
        "popd\n"
        "pwd\n"
        "exit\n");
    // After two pushds and two popds we should be back where we
    // started (d.a).
    EXPECT_NE(r.output.rfind(d.a), std::string::npos);
}

TEST(DirStack, PopdOnEmptyStackErrors) {
    auto r = run_shell("popd\nexit\n");
    EXPECT_NE(r.output.find("directory stack empty"),
              std::string::npos);
}

TEST(DirStack, PushdNonexistentReportsError) {
    auto r = run_shell(
        "pushd /tmp/nonexistent_dir_xyz_888\n"
        "exit\n");
    EXPECT_NE(r.output.find("No such file or directory"),
              std::string::npos);
}

TEST(DirStack, PushdNoArgReportsError) {
    auto r = run_shell("pushd\nexit\n");
    EXPECT_NE(r.output.find("no directory specified"),
              std::string::npos);
}
