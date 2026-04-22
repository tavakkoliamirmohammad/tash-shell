// Tests for tash::cluster::resolve_preset and source_env_file.
//
// TASH_CLUSTER_STOP_HOOKS_DIR is injected as a compile-time define and points
// at data/cluster/stop-hooks/.

#include <gtest/gtest.h>

#include "tash/cluster/presets.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef TASH_CLUSTER_STOP_HOOKS_DIR
#error "TASH_CLUSTER_STOP_HOOKS_DIR must be defined by the build system"
#endif

using namespace tash::cluster;

namespace {

const ResolvedPreset*     as_resolved(const PresetResolveResult& r) { return std::get_if<ResolvedPreset>(&r); }
const PresetResolveError* as_error   (const PresetResolveResult& r) { return std::get_if<PresetResolveError>(&r); }

std::filesystem::path make_temp_dir() {
    const auto tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto d = std::filesystem::temp_directory_path() /
             ("tash_cluster_presets_test_" + tag);
    std::filesystem::create_directories(d);
    return d;
}

}  // namespace

class PresetsTest : public ::testing::Test {
protected:
    std::filesystem::path dir;
    void SetUp()    override { dir = make_temp_dir(); }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

// ── 1. builtin:claude resolves to the packaged script ───────────────

TEST_F(PresetsTest, BuiltinClaudeResolvesToPackagedScript) {
    Preset p;
    p.name      = "claude";
    p.command   = "claude";
    p.stop_hook = "builtin:claude";

    auto r = resolve_preset(p);
    auto* rr = as_resolved(r);
    ASSERT_NE(rr, nullptr) << (as_error(r) ? as_error(r)->message : "");

    EXPECT_EQ(rr->name,    "claude");
    EXPECT_EQ(rr->command, "claude");
    ASSERT_FALSE(rr->stop_hook_path.empty());
    EXPECT_TRUE(std::filesystem::exists(rr->stop_hook_path))
        << "resolved to: " << rr->stop_hook_path;
    EXPECT_NE(rr->stop_hook_path.find("claude-stop-hook.sh"), std::string::npos);
}

// ── 2. unknown builtin:<name> → error ───────────────────────────────

TEST_F(PresetsTest, UnknownBuiltinStopHookRejected) {
    Preset p;
    p.name      = "weird";
    p.command   = "weird";
    p.stop_hook = "builtin:gpt-99";

    auto r = resolve_preset(p);
    auto* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("unknown builtin"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("gpt-99"),          std::string::npos) << err->message;
}

// ── 3. explicit absolute path passes through ────────────────────────

TEST_F(PresetsTest, AbsoluteStopHookPathPassesThrough) {
    const auto script = dir / "custom.sh";
    std::ofstream(script) << "#!/usr/bin/env bash\n";
    std::filesystem::permissions(script,
        std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::add);

    Preset p;
    p.name      = "custom";
    p.command   = "cmd";
    p.stop_hook = script.string();

    auto r = resolve_preset(p);
    auto* rr = as_resolved(r);
    ASSERT_NE(rr, nullptr) << (as_error(r) ? as_error(r)->message : "");
    EXPECT_EQ(rr->stop_hook_path, script.string());
}

// ── 4. relative (non-builtin, non-absolute) stop_hook is rejected ───

TEST_F(PresetsTest, RelativeStopHookRejected) {
    Preset p;
    p.name      = "p";
    p.command   = "c";
    p.stop_hook = "relative/path.sh";

    auto r = resolve_preset(p);
    auto* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("stop_hook"), std::string::npos) << err->message;
}

// ── 5. $VAR expansion in command ────────────────────────────────────

TEST_F(PresetsTest, CommandExpandsEnvVars) {
    ::setenv("TASH_CLUSTER_TEST_CLAUDE_BIN", "/opt/local/bin/claude", 1);

    Preset p;
    p.name    = "c";
    p.command = "$TASH_CLUSTER_TEST_CLAUDE_BIN --resume";

    auto r = resolve_preset(p);
    auto* rr = as_resolved(r);
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(rr->command, "/opt/local/bin/claude --resume");

    ::unsetenv("TASH_CLUSTER_TEST_CLAUDE_BIN");
}

// ── 6. env_file parses key=value (incl. export, quoted values, comments) ──

TEST_F(PresetsTest, EnvFileParsesKeyValuePairs) {
    const auto envfile = dir / "env.sh";
    std::ofstream(envfile) << R"(# leading comment
ANTHROPIC_API_KEY=sk-real-key
export OPENAI_API_KEY="double-quoted"
export SOME_PATH='single-quoted'

# blank line and comment above
HUGGINGFACE_TOKEN=no-quotes
)";
    Preset p;
    p.name     = "p";
    p.command  = "c";
    p.env_file = envfile.string();

    auto r = resolve_preset(p);
    auto* rr = as_resolved(r);
    ASSERT_NE(rr, nullptr) << (as_error(r) ? as_error(r)->message : "");

    EXPECT_EQ(rr->env_vars.at("ANTHROPIC_API_KEY"),   "sk-real-key");
    EXPECT_EQ(rr->env_vars.at("OPENAI_API_KEY"),      "double-quoted");
    EXPECT_EQ(rr->env_vars.at("SOME_PATH"),           "single-quoted");
    EXPECT_EQ(rr->env_vars.at("HUGGINGFACE_TOKEN"),   "no-quotes");
    EXPECT_EQ(rr->env_vars.count("notreal"),          0u);
}

// ── 7. preset with no stop_hook / no env_file leaves them empty ─────

TEST_F(PresetsTest, NoHooksNoEnvStaysEmpty) {
    Preset p;
    p.name    = "minimal";
    p.command = "echo hi";

    auto r = resolve_preset(p);
    auto* rr = as_resolved(r);
    ASSERT_NE(rr, nullptr);
    EXPECT_TRUE(rr->stop_hook_path.empty());
    EXPECT_TRUE(rr->env_vars.empty());
}

// ── 8. missing env_file → error ─────────────────────────────────────

TEST_F(PresetsTest, MissingEnvFileRejected) {
    Preset p;
    p.name     = "p";
    p.command  = "c";
    p.env_file = (dir / "does_not_exist.sh").string();

    auto r = resolve_preset(p);
    auto* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("env_file"),  std::string::npos) << err->message;
}

// ── 9. source_env_file standalone helper ────────────────────────────

TEST_F(PresetsTest, SourceEnvFileStandalone) {
    const auto envfile = dir / "env.sh";
    std::ofstream(envfile) << "A=1\nB=\"two\"\n# comment\n";
    auto m = source_env_file(envfile);
    EXPECT_EQ(m.at("A"), "1");
    EXPECT_EQ(m.at("B"), "two");
    EXPECT_EQ(m.size(), 2u);
}
