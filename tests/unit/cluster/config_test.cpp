// Tests for tash::cluster::ConfigLoader and the associated helpers.
//
// TASH_CLUSTER_FIXTURE_DIR is injected as a compile-time define by
// tash_register_plugin(… TEST_DEFS …) and points at
// tests/fixtures/configs/.

#include <gtest/gtest.h>

#include "tash/cluster/config.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef TASH_CLUSTER_FIXTURE_DIR
#error "TASH_CLUSTER_FIXTURE_DIR must be defined by the build system"
#endif

using namespace tash::cluster;

namespace {

const Config*      as_config(const ConfigLoadResult& r) { return std::get_if<Config>(&r); }
const ConfigError* as_error (const ConfigLoadResult& r) { return std::get_if<ConfigError>(&r); }

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(TASH_CLUSTER_FIXTURE_DIR) / name;
}

}  // namespace

// ── 1. valid minimal round-trips ────────────────────────────────────

TEST(Config, LoadsMinimalValidFromDisk) {
    auto r = ConfigLoader::load(fixture("valid_minimal.toml"));
    const Config* cfg = as_config(r);
    ASSERT_NE(cfg, nullptr) << (as_error(r) ? as_error(r)->format() : "");

    EXPECT_EQ(cfg->defaults.workspace_base, "/tmp/ws");
    EXPECT_EQ(cfg->defaults.default_preset, "claude");

    ASSERT_EQ(cfg->clusters.size(), 1u);
    EXPECT_EQ(cfg->clusters[0].name,     "c1");
    EXPECT_EQ(cfg->clusters[0].ssh_host, "c1.example");

    ASSERT_EQ(cfg->resources.size(), 1u);
    EXPECT_EQ(cfg->resources[0].name, "a100");
    EXPECT_EQ(cfg->resources[0].kind, ResourceKind::Gpu);
    ASSERT_EQ(cfg->resources[0].routes.size(), 1u);
    EXPECT_EQ(cfg->resources[0].routes[0].cluster, "c1");
    EXPECT_EQ(cfg->resources[0].routes[0].gres,    "gpu:a100:1");

    ASSERT_EQ(cfg->presets.size(), 1u);
    EXPECT_EQ(cfg->presets[0].name,    "claude");
    EXPECT_EQ(cfg->presets[0].command, "claude");
    EXPECT_FALSE(cfg->presets[0].env_file.has_value());
    EXPECT_FALSE(cfg->presets[0].stop_hook.has_value());
}

// ── 2. valid full captures every field ──────────────────────────────

TEST(Config, CapturesEveryFieldOfFullExample) {
    auto r = ConfigLoader::load(fixture("valid_full.toml"));
    const Config* cfg = as_config(r);
    ASSERT_NE(cfg, nullptr) << (as_error(r) ? as_error(r)->format() : "");

    EXPECT_EQ(cfg->defaults.workspace_base,     "/scratch/user");
    EXPECT_EQ(cfg->defaults.default_preset,     "claude");
    EXPECT_EQ(cfg->defaults.control_persist,    "4h");
    EXPECT_EQ(cfg->defaults.notify_silence_sec, 300);

    ASSERT_EQ(cfg->clusters.size(), 2u);
    EXPECT_EQ(cfg->clusters[0].name,        "utah-notchpeak");
    EXPECT_EQ(cfg->clusters[0].description, "University of Utah — Notchpeak");
    EXPECT_EQ(cfg->clusters[1].name,        "utah-kingspeak");

    ASSERT_EQ(cfg->resources.size(), 2u);

    const auto& a100 = cfg->resources[0];
    EXPECT_EQ(a100.name,         "a100");
    EXPECT_EQ(a100.kind,         ResourceKind::Gpu);
    EXPECT_EQ(a100.description,  "NVIDIA A100 40GB");
    EXPECT_EQ(a100.default_time, "2:00:00");
    EXPECT_EQ(a100.default_cpus, 8);
    EXPECT_EQ(a100.default_mem,  "32G");
    ASSERT_EQ(a100.routes.size(), 2u);
    EXPECT_EQ(a100.routes[0].cluster,   "utah-notchpeak");
    EXPECT_EQ(a100.routes[1].cluster,   "utah-kingspeak");

    const auto& cpu = cfg->resources[1];
    EXPECT_EQ(cpu.kind, ResourceKind::Cpu);
    ASSERT_EQ(cpu.routes.size(), 1u);
    EXPECT_EQ(cpu.routes[0].gres, "");  // CPU routes omit gres

    ASSERT_EQ(cfg->presets.size(), 2u);
    EXPECT_EQ(cfg->presets[0].env_file.value_or(""),  "/etc/tash/env.sh");
    EXPECT_EQ(cfg->presets[0].stop_hook.value_or(""), "builtin:claude");
    EXPECT_FALSE(cfg->presets[1].env_file.has_value());
    EXPECT_FALSE(cfg->presets[1].stop_hook.has_value());
}

// ── 3. unknown cluster in route ─────────────────────────────────────

TEST(Config, RejectsUnknownClusterInRoute) {
    auto r = ConfigLoader::load(fixture("invalid_unknown_cluster_in_route.toml"));
    const ConfigError* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("unknown cluster"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("ghost"),           std::string::npos) << err->message;
    EXPECT_NE(err->path.find("invalid_unknown_cluster_in_route.toml"), std::string::npos);
}

// ── 4. missing required field (with line) ───────────────────────────

TEST(Config, RejectsMissingRequiredFieldWithLineInfo) {
    auto r = ConfigLoader::load(fixture("invalid_missing_required_field.toml"));
    const ConfigError* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("ssh_host"), std::string::npos) << err->message;
    EXPECT_GT(err->line, 0) << err->format();
}

// ── 5. bad TOML, formatted diagnostic ───────────────────────────────

TEST(Config, FormatsBadTomlWithLineAndColumn) {
    auto r = ConfigLoader::load(fixture("invalid_bad_toml.toml"));
    const ConfigError* err = as_error(r);
    ASSERT_NE(err, nullptr);

    const std::string msg = err->format();
    // Leading prefix + path + colon line/col + message.
    EXPECT_NE(msg.find("tash: cluster: "), std::string::npos) << msg;
    EXPECT_NE(msg.find("invalid_bad_toml.toml"), std::string::npos) << msg;
    EXPECT_GT(err->line, 0) << msg;
}

// ── 6. environment variable expansion ───────────────────────────────

TEST(Config, ExpandsEnvVarsInWorkspaceBaseAndEnvFile) {
    ::setenv("TASH_CLUSTER_TEST_SCRATCH", "/scratch/u999",  1);
    ::setenv("TASH_CLUSTER_TEST_CFGDIR",  "/etc/tash",      1);

    std::string src = R"(
[defaults]
workspace_base = "$TASH_CLUSTER_TEST_SCRATCH"
default_preset = "p"

[[clusters]]
name     = "c1"
ssh_host = "c1.ex"

[[resources]]
name = "r1"
routes = [ { cluster = "c1", account = "a", partition = "p", qos = "q", gres = "" } ]

[[presets]]
name     = "p"
command  = "cmd"
env_file = "${TASH_CLUSTER_TEST_CFGDIR}/env.sh"
)";
    auto r = ConfigLoader::load_from_string(src);
    const Config* cfg = as_config(r);
    ASSERT_NE(cfg, nullptr) << (as_error(r) ? as_error(r)->format() : "");

    EXPECT_EQ(cfg->defaults.workspace_base, "/scratch/u999");
    ASSERT_EQ(cfg->presets.size(), 1u);
    EXPECT_EQ(cfg->presets[0].env_file.value_or(""), "/etc/tash/env.sh");

    ::unsetenv("TASH_CLUSTER_TEST_SCRATCH");
    ::unsetenv("TASH_CLUSTER_TEST_CFGDIR");
}

// ── 7. find_* helpers ───────────────────────────────────────────────

TEST(Config, FindClusterResourcePresetByName) {
    auto r = ConfigLoader::load(fixture("valid_full.toml"));
    const Config* cfg = as_config(r);
    ASSERT_NE(cfg, nullptr);

    EXPECT_NE(find_cluster(*cfg,  "utah-notchpeak"), nullptr);
    EXPECT_NE(find_cluster(*cfg,  "utah-kingspeak"), nullptr);
    EXPECT_EQ(find_cluster(*cfg,  "nope"),            nullptr);

    EXPECT_NE(find_resource(*cfg, "a100"),   nullptr);
    EXPECT_NE(find_resource(*cfg, "cpu-big"),nullptr);
    EXPECT_EQ(find_resource(*cfg, "nope"),   nullptr);

    EXPECT_NE(find_preset(*cfg,   "claude"), nullptr);
    EXPECT_NE(find_preset(*cfg,   "trainer"),nullptr);
    EXPECT_EQ(find_preset(*cfg,   "nope"),   nullptr);
}

// ── 8. rejects bad resource kind ────────────────────────────────────

TEST(Config, RejectsInvalidResourceKind) {
    std::string src = R"(
[[clusters]]
name = "c1"
ssh_host = "c1.ex"

[[resources]]
name = "r1"
kind = "dpu"
routes = [ { cluster = "c1", account = "a", partition = "p", qos = "q", gres = "" } ]

[[presets]]
name = "p"
command = "c"
)";
    auto r = ConfigLoader::load_from_string(src, "t.toml");
    const ConfigError* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("kind"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("gpu"),  std::string::npos) << err->message;
    EXPECT_NE(err->message.find("cpu"),  std::string::npos) << err->message;
}

// ── 9. load() surfaces "cannot open" when file is missing ───────────

TEST(Config, ReportsMissingFile) {
    auto r = ConfigLoader::load(std::filesystem::path("/definitely/does/not/exist.toml"));
    const ConfigError* err = as_error(r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("cannot open"), std::string::npos) << err->message;
}
