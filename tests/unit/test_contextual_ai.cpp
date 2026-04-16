#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include "tash/ai/contextual_ai.h"
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

// ═══════════════════════════════════════════════════════════════
// ? suffix detection (is_ai_question)
// ═══════════════════════════════════════════════════════════════

TEST(ContextualAi, DetectsQuestionSuffix) {
    // Multi-word natural language ending with ?
    // First word must NOT be a valid shell command.
    EXPECT_TRUE(is_ai_question("please find large files?"));
}

TEST(ContextualAi, IgnoresSingleWord) {
    // Single word with ? — no spaces, not natural language
    EXPECT_FALSE(is_ai_question("test?"));
}

TEST(ContextualAi, IgnoresQuotedQuestion) {
    // The ? is inside double quotes — this is a shell command
    EXPECT_FALSE(is_ai_question("echo \"what?\""));
}

TEST(ContextualAi, IgnoresTrailingQuestionInArgs) {
    // "test" is a valid shell builtin, so this is a command not a question
    EXPECT_FALSE(is_ai_question("test -f file?"));
}

TEST(ContextualAi, DetectsMultiWordQuestion) {
    EXPECT_TRUE(is_ai_question("how to compress files?"));
}

// ═══════════════════════════════════════════════════════════════
// Project type detection
// ═══════════════════════════════════════════════════════════════

class ProjectTypeFixture : public ::testing::Test {
protected:
    string test_dir;

    void SetUp() override {
        test_dir = "/tmp/tash_test_project_" + to_string(getpid());
        mkdir(test_dir.c_str(), 0755);
    }

    void TearDown() override {
        // Clean up all possible marker files
        const char *markers[] = {
            "CMakeLists.txt", "Cargo.toml", "package.json",
            "requirements.txt", "pyproject.toml", "go.mod",
            "Makefile", "pom.xml", "build.gradle"
        };
        for (const char *m : markers) {
            string path = test_dir + "/" + m;
            unlink(path.c_str());
        }
        rmdir(test_dir.c_str());
    }

    void create_marker(const string &filename) {
        string path = test_dir + "/" + filename;
        ofstream f(path);
        f << "# marker\n";
        f.close();
    }
};

TEST_F(ProjectTypeFixture, DetectsProjectTypeCMake) {
    create_marker("CMakeLists.txt");
    EXPECT_EQ(detect_project_type(test_dir), "C++ (CMake)");
}

TEST_F(ProjectTypeFixture, DetectsProjectTypeNode) {
    create_marker("package.json");
    EXPECT_EQ(detect_project_type(test_dir), "Node.js");
}

TEST_F(ProjectTypeFixture, DetectsProjectTypeRust) {
    create_marker("Cargo.toml");
    EXPECT_EQ(detect_project_type(test_dir), "Rust");
}

TEST_F(ProjectTypeFixture, DetectsProjectTypePython) {
    create_marker("requirements.txt");
    EXPECT_EQ(detect_project_type(test_dir), "Python");
}

TEST_F(ProjectTypeFixture, DetectsProjectTypeGo) {
    create_marker("go.mod");
    EXPECT_EQ(detect_project_type(test_dir), "Go");
}

TEST_F(ProjectTypeFixture, DetectsNoProjectType) {
    // Empty directory — no marker files
    EXPECT_EQ(detect_project_type(test_dir), "");
}

// ═══════════════════════════════════════════════════════════════
// Git branch detection
// ═══════════════════════════════════════════════════════════════

class GitBranchFixture : public ::testing::Test {
protected:
    string test_dir;
    string git_dir;
    string head_path;

    void SetUp() override {
        test_dir = "/tmp/tash_test_git_" + to_string(getpid());
        git_dir = test_dir + "/.git";
        head_path = git_dir + "/HEAD";
        mkdir(test_dir.c_str(), 0755);
        mkdir(git_dir.c_str(), 0755);
    }

    void TearDown() override {
        unlink(head_path.c_str());
        rmdir(git_dir.c_str());
        rmdir(test_dir.c_str());
    }
};

TEST_F(GitBranchFixture, GitBranchFromHead) {
    {
        ofstream f(head_path);
        f << "ref: refs/heads/main\n";
    }
    EXPECT_EQ(ai_get_git_branch(head_path), "main");
}

TEST_F(GitBranchFixture, GitBranchNoRepo) {
    // Point at a non-existent path
    string nonexistent = "/tmp/tash_test_no_repo_" + to_string(getpid()) + "/.git/HEAD";
    EXPECT_EQ(ai_get_git_branch(nonexistent), "");
}

#else

TEST(ContextualAiDisabled, AiFeaturesNotAvailable) {
    SUCCEED() << "AI features disabled at build time";
}

#endif // TASH_AI_ENABLED
