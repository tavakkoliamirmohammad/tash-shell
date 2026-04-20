#include <gtest/gtest.h>


#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "tash/ai.h"

namespace {

struct KeyPermFixture : public ::testing::Test {
    std::string tmp;
    void SetUp() override {
        const char *base = std::getenv("TMPDIR");
        std::string tbase = (base && *base) ? base : "/tmp";
        tmp = tbase + "/tash_key_perm_" + std::to_string(::getpid()) + "_" +
              std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        std::error_code ec;
        std::filesystem::create_directories(tmp, ec);
        ::setenv("TASH_CONFIG_HOME", tmp.c_str(), 1);
    }
    void TearDown() override {
        ::unsetenv("TASH_CONFIG_HOME");
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
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

