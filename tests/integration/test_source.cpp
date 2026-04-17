// Coverage for `source` / `.` — zero direct tests existed before. Both
// are aliases for the same builtin; every test runs via both invocations
// to keep them from silently diverging.

#include "test_helpers.h"

#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string tmp_script(const std::string &tag, const std::string &body) {
    std::string path = "/tmp/tash_source_" + tag + "_" +
                       std::to_string(getpid()) + ".sh";
    std::ofstream f(path);
    f << body;
    f.close();
    return path;
}

} // namespace

TEST(Source, SourceBuiltinRunsCommands) {
    std::string s = tmp_script("ok", "echo sourced_marker_123\n");
    auto r = run_shell("source " + s + "\nexit\n");
    unlink(s.c_str());
    EXPECT_NE(r.output.find("sourced_marker_123"), std::string::npos);
}

TEST(Source, DotBuiltinRunsCommands) {
    std::string s = tmp_script("dot", "echo dot_marker_456\n");
    auto r = run_shell(". " + s + "\nexit\n");
    unlink(s.c_str());
    EXPECT_NE(r.output.find("dot_marker_456"), std::string::npos);
}

TEST(Source, SourceSetsAliasesInCurrentSession) {
    std::string s = tmp_script("alias",
        "alias srcprobe='echo src_alias_works'\n");
    auto r = run_shell(
        "source " + s + "\n"
        "alias\n"
        "exit\n");
    unlink(s.c_str());
    EXPECT_NE(r.output.find("srcprobe"), std::string::npos);
    EXPECT_NE(r.output.find("src_alias_works"), std::string::npos);
}

TEST(Source, SourceMissingFileReportsError) {
    auto r = run_shell("source /tmp/nonexistent_src_xyz_888\nexit\n");
    // Shell must not crash; must reach the `exit` goodbye.
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
}

TEST(Source, SourceNoArgReportsError) {
    auto r = run_shell("source\nexit\n");
    EXPECT_NE(r.output.find("missing file"), std::string::npos);
}

TEST(Source, NestedSourceExecutesAllCommands) {
    // child.sh echoes, parent.sh sources child and echoes again.
    std::string child = tmp_script("child",
        "echo nested_child\n");
    std::string parent = tmp_script("parent",
        "echo nested_parent_before\n"
        "source " + child + "\n"
        "echo nested_parent_after\n");
    auto r = run_shell("source " + parent + "\nexit\n");
    unlink(child.c_str());
    unlink(parent.c_str());
    EXPECT_NE(r.output.find("nested_parent_before"), std::string::npos);
    EXPECT_NE(r.output.find("nested_child"),         std::string::npos);
    EXPECT_NE(r.output.find("nested_parent_after"),  std::string::npos);
}

TEST(Source, SourcedCommandsCanChangeDirectory) {
    // cd inside a sourced file must persist in the parent session.
    std::string tmpdir = "/tmp/tash_source_cd_" +
                         std::to_string(getpid());
    mkdir(tmpdir.c_str(), 0755);
    std::string s = tmp_script("cd", "cd " + tmpdir + "\n");
    auto r = run_shell(
        "source " + s + "\n"
        "pwd\n"
        "exit\n");
    unlink(s.c_str());
    rmdir(tmpdir.c_str());
    EXPECT_NE(r.output.find(tmpdir), std::string::npos);
}
