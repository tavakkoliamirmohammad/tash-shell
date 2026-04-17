#include <gtest/gtest.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include "tash/core.h"

TEST(HeredocLarge, BodyAbovePipeBufferRoundTrips) {
    std::string body(256 * 1024, '\0');  // 256 KB — above Linux 64 KB pipe buffer
    for (size_t i = 0; i < body.size(); ++i) {
        body[i] = static_cast<char>(i % 251);  // cycle through a prime window
    }
    int fd = open_heredoc_fd(body);
    ASSERT_GE(fd, 0);

    std::string read_back;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        read_back.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);
    EXPECT_EQ(read_back.size(), body.size());
    EXPECT_EQ(read_back, body);
}

TEST(HeredocLarge, EmptyBodyReturnsValidFd) {
    int fd = open_heredoc_fd("");
    ASSERT_GE(fd, 0);
    char c;
    EXPECT_EQ(::read(fd, &c, 1), 0);  // immediate EOF
    ::close(fd);
}
