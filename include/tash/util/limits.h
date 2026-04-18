#ifndef TASH_UTIL_LIMITS_H
#define TASH_UTIL_LIMITS_H

// Runtime caps for pathological-size expansions.
//
// All values are picked high enough that real workloads (building a
// long find(1) argv, interpolating a large $(…), collecting a sizeable
// heredoc) stay well under the cap. The point is to turn
// denial-of-service footguns into clean errors, not to constrain
// ordinary use.

#include <cstddef>

namespace tash::util {

// Maximum combined output length, in bytes, of a single
// expand_variables() or expand_command_substitution() invocation.
// 1 MiB. Beyond this we assume runaway expansion (recursive $(yes)
// substitution, a variable holding a megabyte of text re-interpolated
// dozens of times, …) and bail out with a clear error.
inline constexpr std::size_t TASH_MAX_EXPANSION_BYTES = 1 * 1024 * 1024;

// Maximum number of filenames produced by a single glob expansion.
// 10 000. A typical argv is dozens of entries; 10k is already large
// enough to exceed ARG_MAX on some systems, so going above that gains
// nothing.
inline constexpr std::size_t TASH_MAX_GLOB_RESULTS = 10000;

// Maximum body size, in bytes, of a single heredoc. 100 MiB. The
// body is written to an anonymous tempfile, so memory pressure is
// bounded by the kernel's own tmpfs limits, but we still cap to
// prevent a runaway process from filling /tmp.
inline constexpr std::size_t TASH_MAX_HEREDOC_BYTES = 100ULL * 1024 * 1024;

} // namespace tash::util

#endif // TASH_UTIL_LIMITS_H
