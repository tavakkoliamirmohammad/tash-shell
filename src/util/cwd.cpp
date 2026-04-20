#include "tash/util/cwd.h"
#include "tash/util/io.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace tash::util {

std::string current_working_directory() {
    std::error_code ec;
    auto p = std::filesystem::current_path(ec);
    if (ec) {
        tash::io::debug(std::string("current_path failed: ") + ec.message());
        return {};
    }
    return p.string();
}

} // namespace tash::util
