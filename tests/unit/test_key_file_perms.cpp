#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "tash/ai.h"

namespace {

// Recursively remove directory contents.
static void rm_rf(const std::string &path) {
    DIR *d = ::opendir(path.c_str());
    if (d) {
        struct dirent *e;
        while ((e = ::readdir(d)) != nullptr) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::string child = path + "/" + name;
            struct stat st{};
            if (::lstat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                rm_rf(child);
            } else {
                ::unlink(child.c_str());
            }
        }
        ::closedir(d);
    }
    ::rmdir(path.c_str());
}

struct KeyPermFixture : public ::testing::Test {
    std::string tmp;
    void SetUp() override {
        const char *base = std::getenv("TMPDIR");
        std::string tbase = (base && *base) ? base : "/tmp";
        tmp = tbase + "/tash_key_perm_" + std::to_string(::getpid()) + "_" +
              std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        ::mkdir(tmp.c_str(), 0700);
        ::setenv("TASH_AI_CONFIG_DIR", tmp.c_str(), 1);
    }
    void TearDown() override {
        ::unsetenv("TASH_AI_CONFIG_DIR");
        rm_rf(tmp);
    }
};

} // namespace

TEST_F(KeyPermFixture, SavedKeyIs0600) {
    ASSERT_TRUE(ai_save_provider_key("gemini", "test-key"));
    std::string path = tmp + "/gemini_key";
    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0) << strerror(errno);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}

TEST_F(KeyPermFixture, SavedKeyOverwritesWithTightPerms) {
    std::string path = tmp + "/openai_key";
    {
        std::ofstream f(path);
        f << "stale";
    }
    ::chmod(path.c_str(), 0644);
    ASSERT_TRUE(ai_save_provider_key("openai", "new-key"));
    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}

TEST_F(KeyPermFixture, LegacyAiSaveKeyIs0600) {
    ASSERT_TRUE(ai_save_key("legacy-key"));
    std::string path = tmp + "/ai_key";
    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}

#else

TEST(KeyFilePermsDisabled, AiDisabled) {
    SUCCEED() << "AI features disabled at build time";
}

#endif // TASH_AI_ENABLED
