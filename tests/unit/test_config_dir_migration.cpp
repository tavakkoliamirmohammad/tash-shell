#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include "tash/ai.h"

namespace {

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

} // namespace

TEST(ConfigDirMigration, TightensLooseModeOnKeyWrite) {
    const char *base = std::getenv("TMPDIR");
    std::string tbase = (base && *base) ? base : "/tmp";
    std::string tmp = tbase + "/tash_cfg_mig_" + std::to_string(::getpid()) + "_" +
                      std::to_string(::testing::UnitTest::GetInstance()->random_seed());
    ::mkdir(tmp.c_str(), 0755);
    ::chmod(tmp.c_str(), 0755);
    ::setenv("TASH_AI_CONFIG_DIR", tmp.c_str(), 1);

    // ensure_config_dir is called internally by ai_save_provider_key.
    ASSERT_TRUE(ai_save_provider_key("gemini", "x"));

    struct stat st{};
    ASSERT_EQ(::stat(tmp.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700);

    ::unsetenv("TASH_AI_CONFIG_DIR");
    rm_rf(tmp);
}

#else

TEST(ConfigDirMigrationDisabled, AiDisabled) {
    SUCCEED() << "AI features disabled at build time";
}

#endif // TASH_AI_ENABLED
