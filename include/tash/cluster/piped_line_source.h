// PipedLineSource — fork/exec a shell command, pipe its stdout, read
// lines. Designed for `ssh <cluster> tail -F <event-dir>/**/*.event`
// but works for any line-oriented child.
//
// Lifecycle:
//   - Constructor forks the child. Parent keeps the stdout-read fd.
//   - `next_line()` blocks until one newline-terminated line arrives,
//     or until stop() / child-exit / EOF.
//   - `stop()` closes the read fd and sends SIGTERM to the child, so
//     a concurrent next_line() returns std::nullopt quickly.
//   - Destructor ensures the child is reaped (SIGKILL after a brief
//     grace period if still alive).
//
// Thread safety: one thread may call next_line(); another may call
// stop(). Not safe for concurrent next_line() callers.
//
// This class produces the `LineSource` callable that StreamWatcher
// consumes. Call `as_line_source()` to get one that captures a
// shared_ptr to the source so the underlying process lives as long
// as any reader does.

#ifndef TASH_CLUSTER_PIPED_LINE_SOURCE_H
#define TASH_CLUSTER_PIPED_LINE_SOURCE_H

#include "tash/cluster/stream_watcher.h"

#include <sys/types.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tash::cluster {

class PipedLineSource {
public:
    // argv: full argv for the child (e.g. {"ssh", "c1", "tail", "-F", "/tmp/events"}).
    // Empty argv or fork failure leaves the source in a "closed" state
    // where next_line() immediately returns nullopt.
    explicit PipedLineSource(std::vector<std::string> argv);

    ~PipedLineSource();

    PipedLineSource(const PipedLineSource&)            = delete;
    PipedLineSource& operator=(const PipedLineSource&) = delete;

    // Reads bytes until a newline or EOF. Returns the line without the
    // trailing '\n'. nullopt on EOF, stop(), or I/O error.
    std::optional<std::string> next_line();

    // Idempotent. Closes the read fd (unblocks next_line) and signals
    // the child to terminate.
    void stop();

    // Diagnostics.
    pid_t child_pid() const { return pid_; }
    bool  is_open()   const { return read_fd_ >= 0; }

    // Wraps this source in a LineSource callable. Holds a shared_ptr
    // to the source so the process outlives the callable itself.
    static LineSource as_line_source(std::shared_ptr<PipedLineSource> src);

private:
    std::atomic<bool> stopped_{false};
    pid_t             pid_     = -1;
    int               read_fd_ = -1;
    std::string       rbuf_;
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_PIPED_LINE_SOURCE_H
