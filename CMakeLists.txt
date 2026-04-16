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
    src/core/parser.cpp
    src/core/process.cpp
    src/core/builtins.cpp
    src/ui/prompt.cpp
    src/ui/highlight.cpp
    src/ui/completion.cpp
    src/ui/suggest.cpp
    src/history/history.cpp
    src/history/frecency.cpp
    src/util/colors.cpp
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

    # Create all plugin tests declared via tash_register_plugin(TEST_SOURCES ...)
    tash_finalize_plugin_tests()

    # ── Integration tests ─────────────────────────────────────
    add_executable(test_integration
        tests/integration/test_integration.cpp
        tests/integration/test_basic.cpp
        tests/integration/test_redirection.cpp
        tests/integration/test_background.cpp
        tests/integration/test_cd.cpp
        tests/integration/test_pipes.cpp
        tests/integration/test_history.cpp
        tests/integration/test_env_vars.cpp
        tests/integration/test_glob.cpp
        tests/integration/test_operators.cpp
        tests/integration/test_tashrc.cpp
        tests/integration/test_alias.cpp
        tests/integration/test_script.cpp
        tests/integration/test_comments.cpp
        tests/integration/test_modern_ux.cpp
        tests/integration/test_ai_integration.cpp
        tests/integration/test_contextual_ai.cpp
        tests/integration/test_safety.cpp
        tests/integration/test_explain.cpp
        tests/integration/test_alias_suggest.cpp
        tests/integration/test_clipboard.cpp
        tests/integration/test_builtin_matrix.cpp
        tests/integration/test_benchmark.cpp
    )
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
