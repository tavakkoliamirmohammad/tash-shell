// Verifies that resolve_safe_tmpdir() rejects a world-writable
// or wrong-owner TMPDIR and falls back to /tmp.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "tash/util/safe_tmpdir.h"

namespace fs = std::filesystem;

namespace {
struct SafeTmpDirFixture : public ::testing::Test {
    std::string created;
    const char *original_tmpdir = nullptr;
    void SetUp() override {
        const char *orig = std::getenv("TMPDIR");
        if (orig) original_tmpdir = strdup(orig);
    }
    void TearDown() override {
        if (original_tmpdir) {
            ::setenv("TMPDIR", original_tmpdir, 1);
            free((void *)original_tmpdir);
        } else {
            ::unsetenv("TMPDIR");
        }
        if (!created.empty()) {
            std::error_code ec;
            fs::remove_all(created, ec);
        }
    }
};
} // namespace

TEST_F(SafeTmpDirFixture, WorldWritableTmpdirIsRejected) {
    created = "/tmp/tash_safe_tmpdir_test_" + std::to_string(::getpid());
    std::error_code ec;
    fs::create_directories(created, ec);
    ::chmod(created.c_str(), 0777);

    ::setenv("TMPDIR", created.c_str(), 1);
    std::string resolved = tash::util::resolve_safe_tmpdir();
    EXPECT_EQ(resolved, "/tmp")
        << "world-writable TMPDIR must be rejected, got " << resolved;
}

TEST_F(SafeTmpDirFixture, ProperlyLockedTmpdirIsAccepted) {
    created = "/tmp/tash_safe_tmpdir_trust_" + std::to_string(::getpid());
    std::error_code ec;
    fs::create_directories(created, ec);
    ::chmod(created.c_str(), 0700);

    ::setenv("TMPDIR", created.c_str(), 1);
    std::string resolved = tash::util::resolve_safe_tmpdir();
    EXPECT_EQ(resolved, created);
}

TEST_F(SafeTmpDirFixture, UnsetTmpdirFallsBackToTmp) {
    ::unsetenv("TMPDIR");
    EXPECT_EQ(tash::util::resolve_safe_tmpdir(), "/tmp");
}

TEST_F(SafeTmpDirFixture, NonExistentTmpdirFallsBackToTmp) {
    ::setenv("TMPDIR", "/tmp/tash_safe_tmpdir_does_not_exist_XYZZY", 1);
    EXPECT_EQ(tash::util::resolve_safe_tmpdir(), "/tmp");
}
