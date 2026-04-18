#include <gtest/gtest.h>

#include "tash/util/config_file.h"
#include "tash/util/config_resolver.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

// Scoped stdout/stderr redirect — malformed-JSON path writes a warning,
// and we don't want it cluttering test output. Also lets us inspect it.
class StderrCapture {
public:
    StderrCapture() { old_buf_ = std::cerr.rdbuf(captured_.rdbuf()); }
    ~StderrCapture() { std::cerr.rdbuf(old_buf_); }
    std::string str() const { return captured_.str(); }
private:
    std::stringstream captured_;
    std::streambuf *old_buf_;
};

// Each test gets its own temporary $HOME so `get_data_dir()` resolves
// under it (with XDG/TASH_DATA_HOME cleared). That means writing a
// config at `<home>/.tash/config.json` is the file the loader reads.
class ConfigFileTest : public ::testing::Test {
protected:
    std::string tmp_home;
    std::string saved_home, saved_xdg_data, saved_tash_data, saved_log_level;

    void SetUp() override {
        saved_home       = save("HOME");
        saved_xdg_data   = save("XDG_DATA_HOME");
        saved_tash_data  = save("TASH_DATA_HOME");
        saved_log_level  = save("TASH_LOG_LEVEL");

        unsetenv("XDG_DATA_HOME");
        unsetenv("TASH_DATA_HOME");
        unsetenv("TASH_LOG_LEVEL");

        // PID + nanosecond suffix to avoid collisions between
        // concurrent tests.
        tmp_home = "/tmp/tash_config_file_test_" +
                   std::to_string(::getpid()) + "_" +
                   std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch().count());
        fs::remove_all(tmp_home);
        fs::create_directories(tmp_home + "/.tash");
        setenv("HOME", tmp_home.c_str(), 1);
    }

    void TearDown() override {
        fs::remove_all(tmp_home);
        restore("HOME",           saved_home);
        restore("XDG_DATA_HOME",  saved_xdg_data);
        restore("TASH_DATA_HOME", saved_tash_data);
        restore("TASH_LOG_LEVEL", saved_log_level);
    }

    std::string config_path() const { return tmp_home + "/.tash/config.json"; }

    void write_config(const std::string &body) const {
        std::ofstream out(config_path());
        out << body;
    }

    static std::string save(const char *k) {
        const char *v = std::getenv(k);
        return v ? std::string(v) : std::string("\x01");  // sentinel=unset
    }
    static void restore(const char *k, const std::string &v) {
        if (v == "\x01") unsetenv(k);
        else setenv(k, v.c_str(), 1);
    }
};

// ── Missing file → defaults ────────────────────────────────────

TEST_F(ConfigFileTest, MissingFileYieldsDefaults) {
    // No config.json written.
    auto cfg = tash::config::load();
    EXPECT_TRUE(cfg.disabled_plugins.empty());
    EXPECT_EQ(cfg.log_level, "info");
}

// ── Valid file with both fields ────────────────────────────────

TEST_F(ConfigFileTest, ValidConfigIsParsed) {
    write_config(R"({
        "plugins": { "disabled": ["safety", "fish"] },
        "log_level": "warn"
    })");

    auto cfg = tash::config::load();
    ASSERT_EQ(cfg.disabled_plugins.size(), 2u);
    EXPECT_EQ(cfg.disabled_plugins[0], "safety");
    EXPECT_EQ(cfg.disabled_plugins[1], "fish");
    EXPECT_EQ(cfg.log_level, "warn");
}

// ── Unknown fields are ignored (forward compat) ────────────────

TEST_F(ConfigFileTest, UnknownFieldsAreIgnored) {
    write_config(R"({
        "log_level": "debug",
        "totally_new_setting": { "nested": true }
    })");

    auto cfg = tash::config::load();
    EXPECT_EQ(cfg.log_level, "debug");
    EXPECT_TRUE(cfg.disabled_plugins.empty());
}

// ── Malformed JSON → warning + defaults, no throw ──────────────

TEST_F(ConfigFileTest, MalformedJsonYieldsDefaultsWithWarning) {
    write_config("{ this is not json");

    tash::config::UserConfig cfg;
    std::string stderr_text;
    {
        StderrCapture cap;
        EXPECT_NO_THROW(cfg = tash::config::load());
        stderr_text = cap.str();
    }

    EXPECT_TRUE(cfg.disabled_plugins.empty());
    EXPECT_EQ(cfg.log_level, "info");
    EXPECT_NE(stderr_text.find("tash: warning"), std::string::npos);
    EXPECT_NE(stderr_text.find("config.json"), std::string::npos);
}

// ── TASH_LOG_LEVEL env override wins ───────────────────────────

TEST_F(ConfigFileTest, EnvLogLevelOverridesFile) {
    write_config(R"({ "log_level": "warn" })");
    setenv("TASH_LOG_LEVEL", "debug", 1);

    auto cfg = tash::config::load();
    EXPECT_EQ(cfg.log_level, "debug");
}

// ── TASH_LOG_LEVEL applies even when no file exists ────────────

TEST_F(ConfigFileTest, EnvLogLevelAppliesWithoutFile) {
    setenv("TASH_LOG_LEVEL", "error", 1);

    auto cfg = tash::config::load();
    EXPECT_EQ(cfg.log_level, "error");
    EXPECT_TRUE(cfg.disabled_plugins.empty());
}

// ── Non-string entries inside `plugins.disabled` are skipped ───

TEST_F(ConfigFileTest, NonStringDisabledEntriesAreSkipped) {
    write_config(R"({
        "plugins": { "disabled": ["fig", 42, null, "starship"] }
    })");

    auto cfg = tash::config::load();
    ASSERT_EQ(cfg.disabled_plugins.size(), 2u);
    EXPECT_EQ(cfg.disabled_plugins[0], "fig");
    EXPECT_EQ(cfg.disabled_plugins[1], "starship");
}

// ── loaded() reflects set_loaded() ─────────────────────────────

TEST_F(ConfigFileTest, LoadedAccessorMirrorsSetLoaded) {
    tash::config::UserConfig cfg;
    cfg.disabled_plugins = {"fish"};
    cfg.log_level = "debug";
    tash::config::set_loaded(cfg);

    EXPECT_EQ(tash::config::loaded().log_level, "debug");
    ASSERT_EQ(tash::config::loaded().disabled_plugins.size(), 1u);
    EXPECT_EQ(tash::config::loaded().disabled_plugins[0], "fish");

    // Reset so other tests start from a clean slate.
    tash::config::set_loaded(tash::config::UserConfig{});
}
