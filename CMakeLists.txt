cmake_minimum_required(VERSION 3.17)
project(tash VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
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

# ── Fetch nlohmann/json (used by Fig completion provider + AI) ─
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── AI features (optional, requires OpenSSL) ─────────────────
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    set(TASH_AI_ENABLED ON)
    FetchContent_Declare(
        httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(httplib)

    find_package(CURL REQUIRED)
    find_package(Threads REQUIRED)

    message(STATUS "AI features enabled (OpenSSL + libcurl found)")
else()
    set(TASH_AI_ENABLED OFF)
    message(STATUS "AI features disabled (OpenSSL not found)")
endif()

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
    src/util/config_resolver.cpp
    src/util/theme.cpp
)

# ── Testing option ────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" ON)

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

if(TASH_AI_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_AI_ENABLED)
    target_link_libraries(tash.out PRIVATE
        OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Threads::Threads)
    target_include_directories(tash.out PRIVATE ${httplib_SOURCE_DIR})
endif()

if(TASH_SQLITE_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_SQLITE_ENABLED)
    target_include_directories(tash.out PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(tash.out PRIVATE ${SQLite3_LIBRARIES})
endif()

# ── Test library and plugin tests ─────────────────────────────
if(BUILD_TESTS)
    add_library(shell_lib STATIC ${SHELL_SOURCES})
    target_link_libraries(shell_lib PRIVATE replxx)
    target_include_directories(shell_lib PRIVATE
        ${TASH_INCLUDE_DIRS}
        ${nlohmann_json_SOURCE_DIR}/include)
    target_compile_definitions(shell_lib PRIVATE
        TESTING_BUILD
        TASH_VERSION_STRING="${PROJECT_VERSION}"
        TASH_THEMES_DIR="${CMAKE_SOURCE_DIR}/data/themes")

    if(TASH_AI_ENABLED)
        target_compile_definitions(shell_lib PRIVATE TASH_AI_ENABLED)
        target_link_libraries(shell_lib PRIVATE
            OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Threads::Threads)
        target_include_directories(shell_lib PRIVATE ${httplib_SOURCE_DIR})
    endif()

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
