// PipedLineSource — fork/pipe/read lifecycle tests.
//
// Strategy: spawn a small /bin/sh one-liner that emits known lines and
// observe the parent side. No ssh required; /bin/sh is a guaranteed
// line-emitter on any POSIX host.

#include <gtest/gtest.h>

#include "tash/cluster/piped_line_source.h"

#include <chrono>
#include <thread>
#include <vector>

using namespace tash::cluster;

namespace {

std::vector<std::string> sh(const std::string& script) {
    return {"/bin/sh", "-c", script};
}

}  // namespace

TEST(PipedLineSource, EmitsLinesInOrderUntilEof) {
    PipedLineSource src(sh("printf 'one\\ntwo\\nthree\\n'"));
    ASSERT_TRUE(src.is_open());

    EXPECT_EQ(src.next_line().value_or(""), "one");
    EXPECT_EQ(src.next_line().value_or(""), "two");
    EXPECT_EQ(src.next_line().value_or(""), "three");
    EXPECT_FALSE(src.next_line().has_value());   // EOF
}

TEST(PipedLineSource, FinalLineWithoutNewlineIsReturned) {
    // Last line has no trailing newline — must still surface.
    PipedLineSource src(sh("printf 'a\\nno-eol'"));

    EXPECT_EQ(src.next_line().value_or(""), "a");
    EXPECT_EQ(src.next_line().value_or(""), "no-eol");
    EXPECT_FALSE(src.next_line().has_value());
}

TEST(PipedLineSource, CrlfLineEndingsAreNormalized) {
    PipedLineSource src(sh("printf 'a\\r\\nb\\r\\n'"));

    EXPECT_EQ(src.next_line().value_or(""), "a");   // no trailing \r
    EXPECT_EQ(src.next_line().value_or(""), "b");
}

TEST(PipedLineSource, EmptyArgvFailsClosedGracefully) {
    PipedLineSource src({});
    EXPECT_FALSE(src.is_open());
    EXPECT_FALSE(src.next_line().has_value());
}

TEST(PipedLineSource, ExecFailureSurfacesAsEof) {
    // Nonexistent binary — execvp in child fails, child _exit(127),
    // parent sees pipe close → EOF on read.
    PipedLineSource src({"/does/not/exist/binary"});
    EXPECT_FALSE(src.next_line().has_value());
}

TEST(PipedLineSource, StopUnblocksReader) {
    // Child sleeps forever without emitting. Reader thread blocks in
    // next_line. Main thread calls stop() after 50 ms; reader must
    // return nullopt within a reasonable bound.
    PipedLineSource src(sh("sleep 60"));

    std::atomic<bool> returned{false};
    std::optional<std::string> result;

    std::thread reader([&]() {
        result = src.next_line();
        returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    src.stop();

    for (int i = 0; i < 200 && !returned.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(returned.load(std::memory_order_acquire));
    EXPECT_FALSE(result.has_value());
    reader.join();
}

TEST(PipedLineSource, StopIsIdempotent) {
    PipedLineSource src(sh("sleep 60"));
    src.stop();
    src.stop();   // must not double-close or double-kill
    SUCCEED();
}

TEST(PipedLineSource, DestructorReapsEvenWhenChildIgnoresSigterm) {
    // trap 'echo ignored' TERM; sleep — ignores SIGTERM, so stop's
    // SIGTERM doesn't kill it. Destructor must escalate to SIGKILL
    // and reap within ~100 ms.
    const auto t0 = std::chrono::steady_clock::now();
    {
        PipedLineSource src(sh("trap 'echo ignored' TERM; sleep 60"));
        // Let trap install before we stop().
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }   // ← destructor runs here
    const auto dt = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count(),
              1500);
}

TEST(PipedLineSource, AsLineSourceKeepsProcessAlive) {
    auto src = std::make_shared<PipedLineSource>(sh("printf 'a\\nb\\n'"));
    LineSource fn = PipedLineSource::as_line_source(src);

    // Drop our strong ref; the LineSource must keep src alive.
    src.reset();

    EXPECT_EQ(fn().value_or(""), "a");
    EXPECT_EQ(fn().value_or(""), "b");
    EXPECT_FALSE(fn().has_value());
}

TEST(PipedLineSource, ReadsLinesEmittedOverTime) {
    // Child stretches emissions across ~100 ms; reader must not miss.
    PipedLineSource src(sh("printf 'a\\n'; sleep 0.03; printf 'b\\n'; sleep 0.03; printf 'c\\n'"));

    EXPECT_EQ(src.next_line().value_or(""), "a");
    EXPECT_EQ(src.next_line().value_or(""), "b");
    EXPECT_EQ(src.next_line().value_or(""), "c");
    EXPECT_FALSE(src.next_line().has_value());
}
