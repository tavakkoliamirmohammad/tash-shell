cmake_minimum_required(VERSION 3.17)
project(tash VERSION 1.1.0 LANGUAGES CXX)

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
    message(STATUS "AI features enabled (OpenSSL found)")
else()
    set(TASH_AI_ENABLED OFF)
    message(STATUS "AI features disabled (OpenSSL not found)")
endif()

# ── Common include directories ────────────────────────────────
set(TASH_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/util
    ${replxx_SOURCE_DIR}/include
)

# ── Source files shared by shell binary and test library ─────────
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
)

# ── AI source files (conditional) ────────────────────────────
if(TASH_AI_ENABLED)
    list(APPEND SHELL_SOURCES
        src/ai/ai_handler.cpp
        src/ai/gemini_client.cpp
        src/ai/ollama_client.cpp
        src/ai/ai_setup.cpp
        src/ai/context_suggest.cpp
    )
endif()

# ── Main shell binary ─────────────────────────────────────────
add_executable(tash.out ${SHELL_SOURCES})
target_link_libraries(tash.out PRIVATE replxx)
target_include_directories(tash.out PRIVATE ${TASH_INCLUDE_DIRS})
target_compile_options(tash.out PRIVATE -Wall -Wextra)

if(TASH_AI_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_AI_ENABLED)
    target_link_libraries(tash.out PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_include_directories(tash.out PRIVATE ${httplib_SOURCE_DIR})
endif()

# ── Testing ───────────────────────────────────────────────────
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

    # ── Unit tests (tokenizer, trim, etc.) ────────────────────
    add_library(shell_lib STATIC ${SHELL_SOURCES})
    target_link_libraries(shell_lib PRIVATE replxx)
    target_include_directories(shell_lib PRIVATE ${TASH_INCLUDE_DIRS})
    target_compile_definitions(shell_lib PRIVATE TESTING_BUILD)

    if(TASH_AI_ENABLED)
        target_compile_definitions(shell_lib PRIVATE TASH_AI_ENABLED)
        target_link_libraries(shell_lib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        target_include_directories(shell_lib PRIVATE ${httplib_SOURCE_DIR})
    endif()

    add_executable(test_tokenizer tests/unit/test_tokenizer.cpp)
    target_include_directories(test_tokenizer PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_tokenizer GTest::gtest_main shell_lib)
    gtest_discover_tests(test_tokenizer TEST_PREFIX "unit/")

    add_executable(test_ai tests/unit/test_ai.cpp)
    target_include_directories(test_ai PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_ai GTest::gtest_main shell_lib)
    if(TASH_AI_ENABLED)
        target_compile_definitions(test_ai PRIVATE TASH_AI_ENABLED)
        target_include_directories(test_ai PRIVATE ${httplib_SOURCE_DIR})
    endif()
    gtest_discover_tests(test_ai TEST_PREFIX "unit/ai/")

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
