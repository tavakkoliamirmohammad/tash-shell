#ifndef TASH_UTIL_SAFE_TMPDIR_H
#define TASH_UTIL_SAFE_TMPDIR_H

// Pick a trusted tmp directory for private scratch files.
//
// $TMPDIR is honoured only when it is an existing directory, owned by
// the current uid, with mode 0700 (no group/other bits set). Anything
// else — wrong owner, world-writable, dangling, symlink we can't stat
// safely — falls back to /tmp. Callers should still use mkstemp /
// mkdtemp under the returned directory and unlink immediately.

#include <string>
#include <sys/types.h>

namespace tash::util {

// Returns a trusted directory path (no trailing slash).
std::string resolve_safe_tmpdir();

// Tighten perms on `path` to `mode`. Logs a warning but does not fail
// if chmod is not supported on the underlying filesystem (tmpfs on
// some CI runners, WSL edge cases, etc.).
void ensure_private_perms(const std::string &path, mode_t mode);

} // namespace tash::util

#endif // TASH_UTIL_SAFE_TMPDIR_H
