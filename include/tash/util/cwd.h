#ifndef TASH_UTIL_CWD_H
#define TASH_UTIL_CWD_H

// Safe cwd access that doesn't silently truncate deep paths.
//
// The old code path was:
//   char cwd[MAX_SIZE];        // MAX_SIZE == 1024
//   if (getcwd(cwd, MAX_SIZE)) { ... }
// — fine on typical paths, but a sufficiently deep nested directory
// (HFS+, ext4 with long names, git worktrees under long temp paths)
// causes getcwd to return nullptr with errno=ERANGE and the code
// quietly proceeded as if the cwd were empty: wrong $OLDPWD, wrong
// directory column in history rows, blank prompt, etc.
//
// `current_working_directory()` wraps `std::filesystem::current_path`
// which internally loops getcwd with a growing buffer until it fits.
// Returns "" and logs a debug message on the rare case that even
// filesystem::current_path fails (e.g. the cwd was removed underneath
// the process — EACCES/ENOENT); callers that care should test for
// empty and fall back as before.

#include <string>

namespace tash::util {

// Absolute path of the calling process's current working directory.
// Never truncates. Returns the empty string when the cwd cannot be
// resolved (rare; e.g. after the directory was rmdir'd underneath us).
std::string current_working_directory();

} // namespace tash::util

#endif // TASH_UTIL_CWD_H
