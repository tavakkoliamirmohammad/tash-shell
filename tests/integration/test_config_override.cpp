// Integration: the TASH_CONFIG_HOME / TASH_DATA_HOME env vars actually
// redirect every filesystem touch point (tashrc, theme, sessions, sqlite
// history). Proves the config resolver is wired end-to-end.

#include "test_helpers.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct OverrideGuard {
    std::string tmp_home, tmp_config, tmp_data;
    std::string saved_home, saved_cfg, saved_data;

    OverrideGuard() {
        std::string base = "/tmp/tash_cfg_override_" +
                           std::to_string(getpid());
        tmp_home   = base + "/home";
        tmp_config = base + "/cfg";
        tmp_data   = base + "/data";
        int rc = system(("rm -rf " + base +
                         " && mkdir -p " + tmp_home + " " +
                         tmp_config + " " + tmp_data).c_str());
        (void)rc;

        saved_home = getenv("HOME")              ? getenv("HOME")              : "\x01";
        saved_cfg  = getenv("TASH_CONFIG_HOME")  ? getenv("TASH_CONFIG_HOME")  : "\x01";
        saved_data = getenv("TASH_DATA_HOME")    ? getenv("TASH_DATA_HOME")    : "\x01";

        setenv("HOME",             tmp_home.c_str(),   1);
        setenv("TASH_CONFIG_HOME", tmp_config.c_str(), 1);
        setenv("TASH_DATA_HOME",   tmp_data.c_str(),   1);
    }

    ~OverrideGuard() {
        int rc = system(("rm -rf /tmp/tash_cfg_override_" +
                         std::to_string(getpid())).c_str());
        (void)rc;
        if (saved_home == "\x01") unsetenv("HOME");
        else setenv("HOME", saved_home.c_str(), 1);
        if (saved_cfg == "\x01") unsetenv("TASH_CONFIG_HOME");
        else setenv("TASH_CONFIG_HOME", saved_cfg.c_str(), 1);
        if (saved_data == "\x01") unsetenv("TASH_DATA_HOME");
        else setenv("TASH_DATA_HOME", saved_data.c_str(), 1);
    }
};

bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

} // namespace

TEST(ConfigOverride, SqliteHistoryWritesToTashDataHome) {
    OverrideGuard g;
    run_shell("echo first\necho second\nexit\n");
    EXPECT_TRUE(file_exists(g.tmp_data + "/history.db"))
        << "SqliteHistoryProvider should have written to "
        << g.tmp_data + "/history.db";
    // The default ~/.tash/history.db must NOT exist.
    EXPECT_FALSE(file_exists(g.tmp_home + "/.tash/history.db"));
}

TEST(ConfigOverride, SessionsDirUnderTashDataHome) {
    OverrideGuard g;
    run_shell("alias xprobe=x\nsession save cfgtest\nexit\n");
    EXPECT_TRUE(file_exists(g.tmp_data + "/sessions/cfgtest.json"));
    EXPECT_FALSE(file_exists(g.tmp_home + "/.tash/sessions/cfgtest.json"));
}

TEST(ConfigOverride, ThemeSetPersistsUnderTashConfigHome) {
    OverrideGuard g;
    run_shell("theme set dracula\nexit\n");
    EXPECT_TRUE(file_exists(g.tmp_config + "/theme.toml"));
    EXPECT_TRUE(file_exists(g.tmp_config + "/theme.name"));
    EXPECT_FALSE(file_exists(g.tmp_home + "/.config/tash/theme.toml"));
}

TEST(ConfigOverride, TashrcStaysAtHomeEvenWhenConfigOverridden) {
    OverrideGuard g;
    // Write a .tashrc at $HOME (never moves to TASH_CONFIG_HOME by design).
    {
        std::ofstream rc(g.tmp_home + "/.tashrc");
        rc << "alias rcprobe='echo found'\n";
    }
    auto r = run_shell("alias\nexit\n");
    EXPECT_NE(r.output.find("rcprobe"), std::string::npos)
        << "~/.tashrc should load from $HOME even with TASH_CONFIG_HOME set";
}
