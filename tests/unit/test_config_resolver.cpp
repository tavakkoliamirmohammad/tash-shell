#include <gtest/gtest.h>

#include "tash/util/config_resolver.h"

#include <cstdlib>
#include <string>
#include <sys/stat.h>

using namespace tash::config;

// Each test owns $HOME and the two XDG vars via this fixture so the
// resolver's fallbacks can be exercised deterministically regardless of
// the developer's real environment.
class ConfigResolverTest : public ::testing::Test {
protected:
    std::string saved_home, saved_xdg_config, saved_xdg_data,
                saved_tash_config, saved_tash_data;

    void SetUp() override {
        saved_home         = save("HOME");
        saved_xdg_config   = save("XDG_CONFIG_HOME");
        saved_xdg_data     = save("XDG_DATA_HOME");
        saved_tash_config  = save("TASH_CONFIG_HOME");
        saved_tash_data    = save("TASH_DATA_HOME");

        // Clean slate for every test.
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("XDG_DATA_HOME");
        unsetenv("TASH_CONFIG_HOME");
        unsetenv("TASH_DATA_HOME");
        setenv("HOME", "/tmp/tash_resolver_home", 1);
    }

    void TearDown() override {
        restore("HOME",              saved_home);
        restore("XDG_CONFIG_HOME",   saved_xdg_config);
        restore("XDG_DATA_HOME",     saved_xdg_data);
        restore("TASH_CONFIG_HOME",  saved_tash_config);
        restore("TASH_DATA_HOME",    saved_tash_data);
    }

    static std::string save(const char *k) {
        const char *v = getenv(k);
        return v ? std::string(v) : std::string("\x01");  // sentinel=unset
    }
    static void restore(const char *k, const std::string &v) {
        if (v == "\x01") unsetenv(k);
        else setenv(k, v.c_str(), 1);
    }
};

// ── Default (no XDG, no TASH_*) ────────────────────────────────

TEST_F(ConfigResolverTest, DefaultsFromHome) {
    EXPECT_EQ(get_config_dir(), "/tmp/tash_resolver_home/.config/tash");
    EXPECT_EQ(get_data_dir(),   "/tmp/tash_resolver_home/.tash");
}

// ── XDG precedence ─────────────────────────────────────────────

TEST_F(ConfigResolverTest, XdgConfigHomeTakesPrecedence) {
    setenv("XDG_CONFIG_HOME", "/tmp/xdg_cfg", 1);
    EXPECT_EQ(get_config_dir(), "/tmp/xdg_cfg/tash");
}

TEST_F(ConfigResolverTest, XdgDataHomeTakesPrecedence) {
    setenv("XDG_DATA_HOME", "/tmp/xdg_data", 1);
    EXPECT_EQ(get_data_dir(), "/tmp/xdg_data/tash");
}

// ── TASH_*_HOME beats XDG ──────────────────────────────────────

TEST_F(ConfigResolverTest, TashConfigBeatsXdg) {
    setenv("XDG_CONFIG_HOME",  "/tmp/xdg",  1);
    setenv("TASH_CONFIG_HOME", "/tmp/tash_config_override", 1);
    EXPECT_EQ(get_config_dir(), "/tmp/tash_config_override");
}

TEST_F(ConfigResolverTest, TashDataBeatsXdg) {
    setenv("XDG_DATA_HOME",  "/tmp/xdg",  1);
    setenv("TASH_DATA_HOME", "/tmp/tash_data_override", 1);
    EXPECT_EQ(get_data_dir(), "/tmp/tash_data_override");
}

// ── Derived paths follow the base dirs ─────────────────────────

TEST_F(ConfigResolverTest, ThemePathsUnderConfigDir) {
    setenv("TASH_CONFIG_HOME", "/cfg", 1);
    EXPECT_EQ(get_theme_toml_path(), "/cfg/theme.toml");
    EXPECT_EQ(get_theme_name_path(), "/cfg/theme.name");
    EXPECT_EQ(get_user_themes_dir(), "/cfg/themes");
}

TEST_F(ConfigResolverTest, DataPathsUnderDataDir) {
    setenv("TASH_DATA_HOME", "/data", 1);
    EXPECT_EQ(get_sessions_dir(),    "/data/sessions");
    EXPECT_EQ(get_history_db_path(), "/data/history.db");
}

TEST_F(ConfigResolverTest, FigCompletionsUnderConfig) {
    setenv("TASH_CONFIG_HOME", "/cfg", 1);
    EXPECT_EQ(get_fig_completions_dir(), "/cfg/completions/fig");
}

// ── ~/.tashrc, ~/.tash_history, ~/.tash_z stay at $HOME ───────

TEST_F(ConfigResolverTest, TashrcStaysAtHome) {
    setenv("TASH_CONFIG_HOME", "/cfg", 1);  // should NOT affect ~/.tashrc
    EXPECT_EQ(get_tashrc_path(),       "/tmp/tash_resolver_home/.tashrc");
    EXPECT_EQ(get_history_file_path(), "/tmp/tash_resolver_home/.tash_history");
    EXPECT_EQ(get_frecency_path(),     "/tmp/tash_resolver_home/.tash_z");
}

// ── ensure_dir creates nested paths ────────────────────────────

TEST_F(ConfigResolverTest, EnsureDirCreatesNested) {
    std::string base = "/tmp/tash_resolver_test_" +
                       std::to_string(getpid()) + "/a/b/c";
    // Pre-cleanup
    int rc = system(("rm -rf /tmp/tash_resolver_test_" +
                    std::to_string(getpid())).c_str());
    (void)rc;

    EXPECT_TRUE(ensure_dir(base));
    struct stat st;
    EXPECT_EQ(stat(base.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));

    // Idempotent.
    EXPECT_TRUE(ensure_dir(base));

    rc = system(("rm -rf /tmp/tash_resolver_test_" +
                std::to_string(getpid())).c_str());
    (void)rc;
}
