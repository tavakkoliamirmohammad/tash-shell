cmake_minimum_required(VERSION 3.17)
project(tash VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Fetch replxx (replaces readline/libedit) ──────────────────
include(FetchContent)
FetchContent_Declare(
    replxx
    URL https://github.com/AmokHuginnsson/replxx/archive/refs/tags/release-0.0.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# replxx 0.0.4 uses cmake_minimum_required(<3.10); suppress the deprecation
# warning until upstream cuts a new release
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(replxx)
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)
# Suppress C++20 compat warnings from replxx internals
if(TARGET replxx)
    target_compile_options(replxx PRIVATE -Wno-c++20-compat)
endif()

# ── nlohmann/json (used by Fig completion provider + AI) ────
# Prefer the distro package when installed — Debian/Ubuntu ship
# `nlohmann-json3-dev`, Fedora/RHEL/Rocky/Alma ship `json-devel`,
# Homebrew ships `nlohmann-json`. That shaves 1-2s off configure
# and stops us bundling our own copy when the system already has
# one. FetchContent is a strict fallback for environments without
# the package (CI containers that haven't installed it, etc.).
find_package(nlohmann_json 3.9.0 CONFIG QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann_json ${nlohmann_json_VERSION} found (system)")
    # Normalize the include-dir variable used below.
    get_target_property(nlohmann_json_SOURCE_DIR nlohmann_json::nlohmann_json
                        INTERFACE_INCLUDE_DIRECTORIES)
else()
    message(STATUS "nlohmann_json not found — fetching from upstream")
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
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

# ── Common include directories ────────────────────────────────
set(TASH_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/util
    ${replxx_SOURCE_DIR}/include
)

# ── Core shell sources (non-plugin) ────────────────────────────
set(SHELL_SOURCES
    src/main.cpp
    src/repl.cpp
    src/startup.cpp
    src/ai/ai_bootstrap.cpp
    src/core/executor.cpp
    src/core/signals.cpp
    src/core/parser.cpp
    src/core/process.cpp
    src/core/builtins.cpp
    src/builtins/nav.cpp
    src/builtins/env.cpp
    src/builtins/bg.cpp
    src/builtins/history.cpp
    src/builtins/ui.cpp
    src/builtins/shell.cpp
    src/ui/prompt.cpp
    src/ui/highlight.cpp
    src/ui/completion.cpp
    src/ui/suggest.cpp
    src/history/history.cpp
    src/history/frecency.cpp
    src/util/colors.cpp
    src/util/config_file.cpp
    src/util/config_resolver.cpp
    src/util/safe_exec.cpp
    src/util/safe_tmpdir.cpp
    src/util/theme.cpp
)

# ── Testing option ────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" ON)

# ── Sanitizers (ASan + UBSan) ─────────────────────────────────
# Opt-in instrumentation for CI and local debugging. Intentionally
# applied via add_compile_options/add_link_options so every target —
# tash.out, shell_lib, test_* — picks the flags up without each one
# having to opt in individually. Let CMAKE_BUILD_TYPE drive -O/-g;
# RelWithDebInfo gives readable stack traces without killing speed.
option(TASH_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(TASH_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "TASH_SANITIZERS is not supported on MSVC")
    endif()
    message(STATUS "Sanitizers enabled: address,undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ── Coverage (gcov/lcov) ──────────────────────────────────────
# --coverage is the portable spelling; GCC and Clang both accept it
# and emit gcov-compatible notes/data files. MSVC has no equivalent,
# so we hard-gate to GCC/Clang.
option(TASH_COVERAGE "Enable gcov-compatible coverage instrumentation" OFF)
if(TASH_COVERAGE)
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
            CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        message(FATAL_ERROR
            "TASH_COVERAGE requires GCC or Clang (got ${CMAKE_CXX_COMPILER_ID})")
    endif()
    message(STATUS "Coverage instrumentation enabled (--coverage)")
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

if(BUILD_TESTS)
    enable_testing()
    include(GoogleTest)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ── Plugin registry + list ────────────────────────────────────
# Each plugin registers itself in cmake/plugin_list.cmake. Adding a new
# plugin means appending one tash_register_plugin(...) call at the bottom
# of that file, which avoids per-PR merge conflicts.
include(${CMAKE_SOURCE_DIR}/cmake/plugins.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/plugin_list.cmake)

# ── Generate man page with version ────────────────────────────
configure_file(tash.1.in tash.1 @ONLY)

# ── Main shell binary ─────────────────────────────────────────
add_executable(tash.out ${SHELL_SOURCES})
target_link_libraries(tash.out PRIVATE replxx)
target_include_directories(tash.out PRIVATE
    ${TASH_INCLUDE_DIRS}
    ${nlohmann_json_SOURCE_DIR}/include)
target_compile_options(tash.out PRIVATE -Wall -Wextra)
target_compile_definitions(tash.out PRIVATE
    TASH_VERSION_STRING="${PROJECT_VERSION}"
    TASH_THEMES_DIR="${CMAKE_SOURCE_DIR}/data/themes")

target_compile_definitions(tash.out PRIVATE TASH_AI_ENABLED)
target_link_libraries(tash.out PRIVATE CURL::libcurl Threads::Threads)

if(TASH_SQLITE_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_SQLITE_ENABLED)
    target_include_directories(tash.out PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(tash.out PRIVATE ${SQLite3_LIBRARIES})
endif()

# ── Test library and plugin tests ─────────────────────────────
if(BUILD_TESTS)
    # Same sources as tash.out minus main.cpp. Under TESTING_BUILD
    # main.cpp's int main() is #ifdef'd out, so including it in the
    # static lib would have ranlib warn "has no symbols". Computed
    # here (not earlier) so plugin SOURCES from tash_register_plugin
    # are already appended to SHELL_SOURCES.
    set(SHELL_LIB_SOURCES ${SHELL_SOURCES})
    list(REMOVE_ITEM SHELL_LIB_SOURCES src/main.cpp)
    add_library(shell_lib STATIC ${SHELL_LIB_SOURCES})
    target_link_libraries(shell_lib PRIVATE replxx)
    target_include_directories(shell_lib PRIVATE
        ${TASH_INCLUDE_DIRS}
        ${nlohmann_json_SOURCE_DIR}/include)
    target_compile_definitions(shell_lib PRIVATE
        TESTING_BUILD
        TASH_VERSION_STRING="${PROJECT_VERSION}"
        TASH_THEMES_DIR="${CMAKE_SOURCE_DIR}/data/themes")

    target_compile_definitions(shell_lib PRIVATE TASH_AI_ENABLED)
    target_link_libraries(shell_lib PRIVATE CURL::libcurl Threads::Threads)

    if(TASH_SQLITE_ENABLED)
        target_compile_definitions(shell_lib PRIVATE TASH_SQLITE_ENABLED)
        target_include_directories(shell_lib PRIVATE ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(shell_lib PRIVATE ${SQLite3_LIBRARIES})
    endif()

    add_executable(test_tokenizer tests/unit/test_tokenizer.cpp)
    target_include_directories(test_tokenizer PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_tokenizer GTest::gtest_main shell_lib)
    gtest_discover_tests(test_tokenizer TEST_PREFIX "unit/")

    add_executable(test_parser_properties tests/unit/test_parser_properties.cpp)
    target_include_directories(test_parser_properties PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_parser_properties GTest::gtest_main shell_lib)
    gtest_discover_tests(test_parser_properties TEST_PREFIX "unit/")

    # Create all plugin tests declared via tash_register_plugin(TEST_SOURCES ...)
    tash_finalize_plugin_tests()

    # ── Integration tests ─────────────────────────────────────
    # Auto-discover every test_*.cpp under tests/integration/ so new
    # test files don't require CMakeLists edits. CONFIGURE_DEPENDS
    # forces a re-glob on file-add/remove so builds stay correct.
    file(GLOB TASH_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/tests/integration/test_*.cpp)
    add_executable(test_integration ${TASH_INTEGRATION_TEST_SOURCES})
    target_include_directories(test_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
        ${TASH_INCLUDE_DIRS}
    )
    target_link_libraries(test_integration GTest::gtest)
    gtest_discover_tests(test_integration
        TEST_PREFIX "integration/"
        PROPERTIES ENVIRONMENT "TASH_SHELL_BIN=$<TARGET_FILE:tash.out>"
    )
endif()

# ── libFuzzer harness (clang-only, opt-in) ─────────────────────
# Build with:
#   cmake -S . -B build-fuzz -DTASH_ENABLE_FUZZER=ON \
#         -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTS=OFF
#   cmake --build build-fuzz --target tash_parser_fuzzer
# Run:
#   ./build-fuzz/tash_parser_fuzzer fuzz/corpus -max_total_time=60
option(TASH_ENABLE_FUZZER "Build the libFuzzer parser harness (clang required)" OFF)
if(TASH_ENABLE_FUZZER)
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
            CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"))
        message(FATAL_ERROR
            "TASH_ENABLE_FUZZER requires clang (got ${CMAKE_CXX_COMPILER_ID})")
    endif()
    # Parse-only surface: just parser.cpp. tash/core.h declares symbols
    # defined elsewhere (write_stderr inline, builtins, etc.) but the
    # parser never calls them, so the linker resolves without the rest
    # of the shell. Keeps instrumentation tight.
    set(TASH_FUZZ_SOURCES
        src/core/parser.cpp
    )
    add_executable(tash_parser_fuzzer fuzz/parser_fuzz.cpp ${TASH_FUZZ_SOURCES})
    target_include_directories(tash_parser_fuzzer PRIVATE
        ${TASH_INCLUDE_DIRS}
        ${nlohmann_json_SOURCE_DIR}/include)
    target_compile_definitions(tash_parser_fuzzer PRIVATE
        TESTING_BUILD
        TASH_VERSION_STRING="${PROJECT_VERSION}"
        TASH_THEMES_DIR="${CMAKE_SOURCE_DIR}/data/themes")
    target_compile_options(tash_parser_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined -g -O1)
    target_link_options(tash_parser_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined)
    target_link_libraries(tash_parser_fuzzer PRIVATE replxx)
endif()
