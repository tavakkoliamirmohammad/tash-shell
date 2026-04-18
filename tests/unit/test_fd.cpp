// Tests for tash::util::FileDescriptor: RAII close, move semantics,
// release() ownership transfer.

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include "tash/util/fd.h"

using tash::util::FileDescriptor;

TEST(FileDescriptor, DefaultConstructedIsInvalid) {
    FileDescriptor fd;
    EXPECT_FALSE(fd.valid());
    EXPECT_EQ(fd.get(), -1);
    EXPECT_FALSE(static_cast<bool>(fd));
}

TEST(FileDescriptor, ClosesOnScopeExit) {
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);

    {
        FileDescriptor writer(pipefd[1]);
        EXPECT_TRUE(writer.valid());
        // writer is closed here.
    }

    // Reader should now see EOF (read returns 0 once the write end closes).
    char buf[1];
    ssize_t n = ::read(pipefd[0], buf, 1);
    EXPECT_EQ(n, 0) << "writer end wasn't closed on destruction";

    ::close(pipefd[0]);
}

TEST(FileDescriptor, MoveTransfersOwnership) {
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);

    FileDescriptor a(pipefd[1]);
    int raw = a.get();

    FileDescriptor b(std::move(a));
    EXPECT_FALSE(a.valid()) << "source must be cleared by move";
    EXPECT_TRUE(b.valid());
    EXPECT_EQ(b.get(), raw);

    // Closing b (via destructor when this scope ends) must close the fd —
    // re-use via reset() to get the assertion into the current scope.
    b.reset();
    char buf[1];
    EXPECT_EQ(::read(pipefd[0], buf, 1), 0);
    ::close(pipefd[0]);
}

TEST(FileDescriptor, MoveAssignClosesExistingFd) {
    int pipe1[2], pipe2[2];
    ASSERT_EQ(::pipe(pipe1), 0);
    ASSERT_EQ(::pipe(pipe2), 0);

    FileDescriptor a(pipe1[1]);
    FileDescriptor b(pipe2[1]);
    a = std::move(b);

    // pipe1 write end was held by `a`, must be closed now.
    char buf[1];
    EXPECT_EQ(::read(pipe1[0], buf, 1), 0);
    // `a` now owns pipe2 write end — destruction will close it.
    ::close(pipe1[0]);

    a.reset();
    EXPECT_EQ(::read(pipe2[0], buf, 1), 0);
    ::close(pipe2[0]);
}

TEST(FileDescriptor, ReleaseDoesNotClose) {
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);

    int raw = -1;
    {
        FileDescriptor fd(pipefd[1]);
        raw = fd.release();
        EXPECT_FALSE(fd.valid());
        EXPECT_EQ(fd.get(), -1);
        // fd's destructor runs here — must NOT close `raw`.
    }

    // Still writable — release transferred ownership out.
    ssize_t w = ::write(raw, "x", 1);
    EXPECT_EQ(w, 1);
    ::close(raw);

    char buf[1];
    ASSERT_EQ(::read(pipefd[0], buf, 1), 1);
    EXPECT_EQ(buf[0], 'x');
    ::close(pipefd[0]);
}

TEST(FileDescriptor, ResetOnEmptyIsNoop) {
    FileDescriptor fd;
    fd.reset();
    EXPECT_FALSE(fd.valid());
}
