#ifndef TASH_UTIL_FD_H
#define TASH_UTIL_FD_H

// Minimal RAII wrapper for POSIX file descriptors.
//
// Closes the fd on destruction unless release() has transferred ownership
// first. Non-copyable, move-only — matches unique_ptr semantics, just
// specialised for the int + close() case. Deep-review finding: C3.4.
//
// Intentionally header-only: trivial enough that inlining wins and the
// alternative (a .cpp) would force every TU that just needs the type to
// link an extra library.

#include <unistd.h>
#include <utility>

namespace tash::util {

class FileDescriptor {
    int fd_ = -1;

public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd) : fd_(fd) {}

    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }

    FileDescriptor(FileDescriptor &&o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    FileDescriptor &operator=(FileDescriptor &&o) noexcept {
        if (this != &o) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = o.fd_;
            o.fd_ = -1;
        }
        return *this;
    }

    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    int get() const noexcept { return fd_; }

    // Relinquish ownership — caller becomes responsible for close().
    int release() noexcept {
        int x = fd_;
        fd_ = -1;
        return x;
    }

    // Close the managed fd now (no-op if empty).
    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return fd_ >= 0; }
};

} // namespace tash::util

#endif // TASH_UTIL_FD_H
