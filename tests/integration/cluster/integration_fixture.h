// Common RAII fixture for Tier-2 integration tests.
//
// Prepends tests/fakes/bin/ to $PATH (so ssh / sbatch / squeue / sinfo
// / scancel / tmux / osascript / notify-send resolve to our stubs),
// points $TASH_FAKE_SCENARIO at a test-managed scenario file, and
// $TASH_FAKE_LOG at a per-test log. Restores the original values in
// TearDown.
//
// Scenarios are bash fragments:
//   SSH_STDOUT=..., SSH_EXIT=...
//   ssh_stdout_sbatch=..., ssh_exit_sbatch=...    (per-ssh-subcommand)
// See tests/fakes/bin/_stub_runner.sh.

#ifndef TASH_CLUSTER_TEST_INTEGRATION_FIXTURE_H
#define TASH_CLUSTER_TEST_INTEGRATION_FIXTURE_H

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef TASH_CLUSTER_STUB_BIN_DIR
#error "TASH_CLUSTER_STUB_BIN_DIR must be defined by the build system"
#endif

namespace tash::cluster::testing {

class IntegrationFixture : public ::testing::Test {
protected:
    std::filesystem::path  tmp_dir;
    std::filesystem::path  scenario_path;
    std::filesystem::path  log_path;
    std::string             saved_path;
    std::string             saved_scenario;
    std::string             saved_log;

    void SetUp() override {
        const auto tag = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        tmp_dir       = std::filesystem::temp_directory_path() /
                         ("tash_cluster_integ_" + tag);
        std::filesystem::create_directories(tmp_dir);
        scenario_path = tmp_dir / "scenario.sh";
        log_path      = tmp_dir / "stubs.log";
        // Start with an empty scenario; tests populate via set_scenario.
        std::ofstream(scenario_path).close();

        saved_path = std::getenv("PATH") ? std::getenv("PATH") : "";
        {
            const auto* s = std::getenv("TASH_FAKE_SCENARIO");
            saved_scenario = s ? s : "";
        }
        {
            const auto* l = std::getenv("TASH_FAKE_LOG");
            saved_log = l ? l : "";
        }

        const std::string new_path =
            std::string(TASH_CLUSTER_STUB_BIN_DIR) + ":" + saved_path;
        ::setenv("PATH",                 new_path.c_str(),            1);
        ::setenv("TASH_FAKE_SCENARIO",   scenario_path.c_str(),        1);
        ::setenv("TASH_FAKE_LOG",        log_path.c_str(),             1);
    }

    void TearDown() override {
        ::setenv("PATH", saved_path.c_str(), 1);
        if (saved_scenario.empty()) ::unsetenv("TASH_FAKE_SCENARIO");
        else                          ::setenv("TASH_FAKE_SCENARIO",
                                                 saved_scenario.c_str(), 1);
        if (saved_log.empty()) ::unsetenv("TASH_FAKE_LOG");
        else                     ::setenv("TASH_FAKE_LOG",
                                            saved_log.c_str(),            1);
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }

    // Overwrite scenario_path with `body`. Caller is responsible for
    // valid bash syntax; typically just a sequence of VAR=value lines.
    void set_scenario(const std::string& body) {
        std::ofstream f(scenario_path, std::ios::trunc);
        f << body;
    }

    std::string read_log() const {
        std::ifstream f(log_path);
        std::ostringstream b; b << f.rdbuf(); return b.str();
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_TEST_INTEGRATION_FIXTURE_H
