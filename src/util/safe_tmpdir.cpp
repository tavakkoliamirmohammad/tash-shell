#include "tash/util/safe_tmpdir.h"

#include "tash/core.h"  // for write_stderr

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace tash::util {

static bool tmpdir_is_trusted(const char *dir) {
    if (!dir || !*dir) return false;
    struct stat st{};
    // lstat so a symlink does NOT silently get followed; if TMPDIR is a
    // symlink we refuse it. Real directories only.
    if (::lstat(dir, &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return false;
    if (st.st_uid != ::getuid()) return false;
    mode_t perms = st.st_mode & 0777;
    if (perms != 0700) return false;
    return true;
}

std::string resolve_safe_tmpdir() {
    const char *tmpdir = std::getenv("TMPDIR");
    if (tmpdir_is_trusted(tmpdir)) {
        std::string s(tmpdir);
        // Strip any trailing slash so callers can append "/tash-..." unconditionally.
        while (s.size() > 1 && s.back() == '/') s.pop_back();
        return s;
    }
    return "/tmp";
}

void ensure_private_perms(const std::string &path, mode_t mode) {
    if (path.empty()) return;
    if (::chmod(path.c_str(), mode) != 0) {
        // tmpfs on some CI runners silently rejects chmod. Log once per
        // path but continue -- a missing chmod is not a security
        // regression over the previous umask-only behaviour.
        write_stderr("tash: warning: could not tighten permissions on " +
                     path + ": " + std::strerror(errno) + "\n");
    }
}

} // namespace tash::util
