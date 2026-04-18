# Feature detection: compilers' build-type default, system libraries,
# and the derived TASH_*_ENABLED flags the main target keys off.
#
# After this include, the following are defined/available:
#   CMAKE_BUILD_TYPE             — defaulted to Release on single-config gens
#   CURL::libcurl, Threads       — find_package() imported targets
#   SQLite3_FOUND, SQLite3_*     — optional SQLite history backend
#   nlohmann_json_FOUND (+SDIR)  — system package if present; fetched in
#                                  fetch_deps.cmake otherwise
#   TASH_AI_ENABLED              — always ON (libcurl is required)
#   TASH_SQLITE_ENABLED          — ON iff SQLite3 was found
#   TASH_NEED_FETCH_JSON         — internal: true iff system package missing

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── nlohmann/json (used by Fig completion provider + AI) ────
# Prefer the distro package when installed — Debian/Ubuntu ship
# `nlohmann-json3-dev`, Fedora/RHEL/Rocky/Alma ship `json-devel`,
# Homebrew ships `nlohmann-json`. That shaves 1-2s off configure
# and stops us bundling our own copy when the system already has
# one. FetchContent is a strict fallback for environments without
# the package (CI containers that haven't installed it, etc.) and
# lives in cmake/fetch_deps.cmake.
find_package(nlohmann_json 3.9.0 CONFIG QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann_json ${nlohmann_json_VERSION} found (system)")
    # Normalize the include-dir variable used by the main target.
    get_target_property(nlohmann_json_SOURCE_DIR nlohmann_json::nlohmann_json
                        INTERFACE_INCLUDE_DIRECTORIES)
    set(TASH_NEED_FETCH_JSON FALSE)
else()
    message(STATUS "nlohmann_json not found — will fetch from upstream")
    set(TASH_NEED_FETCH_JSON TRUE)
endif()

# ── AI features (libcurl hard dependency) ────────────────────
# libcurl handles HTTPS; its SSL backend (OpenSSL 1.1, OpenSSL 3,
# NSS, GnuTLS — distro's choice) is abstracted away from us. No
# direct OpenSSL link needed anymore now that cpp-httplib is gone.
# Minimum 7.61 matches AlmaLinux 8 / manylinux_2_28 baseline —
# every supported distro ships at least this.
find_package(CURL 7.61 REQUIRED)
find_package(Threads REQUIRED)
set(TASH_AI_ENABLED ON)
message(STATUS "AI features enabled (libcurl ${CURL_VERSION_STRING})")

# ── SQLite history (optional) ─────────────────────────────────
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(TASH_SQLITE_ENABLED ON)
    message(STATUS "SQLite history enabled (SQLite3 found)")
else()
    set(TASH_SQLITE_ENABLED OFF)
    message(STATUS "SQLite history disabled (SQLite3 not found)")
endif()
