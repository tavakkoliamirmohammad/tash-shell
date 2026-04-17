// Glob + tokenization behavior on filenames that stress the bytes-vs-
// codepoints boundary: UTF-8 multibyte, embedded spaces, and the
// pathological case of an embedded newline (POSIX allows it; most
// shells fail in "interesting" ways). We don't require perfect
// bash-compat here — the bar is "no crash, command output includes
// the file we created".

#include "test_helpers.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct Sandbox {
    std::string root;
    Sandbox() {
        root = "/tmp/tash_unicode_" + std::to_string(getpid());
        int rc = ::system(("rm -rf " + root + " && mkdir -p " + root).c_str());
        (void)rc;
    }
    ~Sandbox() {
        int rc = ::system(("rm -rf " + root).c_str());
        (void)rc;
    }
    void touch(const std::string &relpath) {
        std::ofstream f(root + "/" + relpath);
        f << "x";
    }
};

} // namespace

TEST(UnicodePaths, MultibyteFilenameMatchesGlob) {
    Sandbox s;
    s.touch("\xe6\x97\xa5\xe8\xa8\x98.txt");       // UTF-8 "日記.txt"
    s.touch("\xe2\x98\x83.log");                   // UTF-8 "☃.log"
    auto r = run_shell("ls " + s.root + "/*.txt\nexit\n");
    // File must appear in output. We check by the hex sequence so the
    // test file itself doesn't depend on the editor's encoding.
    EXPECT_NE(r.output.find("\xe6\x97\xa5\xe8\xa8\x98.txt"),
              std::string::npos);
}

TEST(UnicodePaths, MultibyteDirectoryNavigation) {
    Sandbox s;
    std::string subdir = s.root + "/\xce\xb1\xce\xb2\xce\xb3"; // αβγ
    std::filesystem::create_directories(subdir);
    std::ofstream f(subdir + "/probe.txt"); f << "hi"; f.close();

    auto r = run_shell("ls " + subdir + "\nexit\n");
    EXPECT_NE(r.output.find("probe.txt"), std::string::npos);
}

TEST(UnicodePaths, EmojiFilenameInGlob) {
    Sandbox s;
    s.touch("\xf0\x9f\x94\xa5.sh");                // 🔥.sh
    s.touch("plain.sh");
    auto r = run_shell("ls " + s.root + "/*.sh\nexit\n");
    EXPECT_NE(r.output.find("\xf0\x9f\x94\xa5.sh"), std::string::npos);
    EXPECT_NE(r.output.find("plain.sh"),             std::string::npos);
}

TEST(UnicodePaths, SpaceInFilenameRequiresQuoting) {
    // Quoted filename with a space is a single argument; without
    // quotes, it becomes two args. We test the quoted form survives
    // tokenization and reaches ls.
    Sandbox s;
    s.touch("has space.txt");
    auto r = run_shell(
        "ls \"" + s.root + "/has space.txt\"\n"
        "exit\n");
    EXPECT_NE(r.output.find("has space.txt"), std::string::npos);
    EXPECT_EQ(r.output.find("No such file"),  std::string::npos);
}

TEST(UnicodePaths, GlobWithMultibyteStarPrefix) {
    Sandbox s;
    s.touch("\xe6\x97\xa5\xe8\xa8\x98_a.txt");
    s.touch("\xe6\x97\xa5\xe8\xa8\x98_b.txt");
    s.touch("latin_c.txt");
    // Glob by non-ASCII prefix.
    auto r = run_shell("ls " + s.root + "/\xe6\x97\xa5\xe8\xa8\x98*\nexit\n");
    EXPECT_NE(r.output.find("\xe6\x97\xa5\xe8\xa8\x98_a.txt"),
              std::string::npos);
    EXPECT_NE(r.output.find("\xe6\x97\xa5\xe8\xa8\x98_b.txt"),
              std::string::npos);
    EXPECT_EQ(r.output.find("latin_c.txt"), std::string::npos);
}
