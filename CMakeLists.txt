cmake_minimum_required(VERSION 3.17)
project(tash VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Find readline portably
find_library(READLINE_LIB readline REQUIRED)
find_path(READLINE_INCLUDE readline/readline.h)

# ── Source files shared by shell binary and test library ─────────
set(SHELL_SOURCES
    main.cpp
    colors.cpp
    prompt.cpp
    parser.cpp
    builtins.cpp
    process.cpp
)

# ── Main shell binary ─────────────────────────────────────────
add_executable(shell.out ${SHELL_SOURCES})
target_link_libraries(shell.out PRIVATE ${READLINE_LIB})
target_include_directories(shell.out PRIVATE ${READLINE_INCLUDE})
target_compile_options(shell.out PRIVATE -Wall -Wextra)

# ── Testing ───────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    # Prevent GoogleTest from overriding compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # ── Unit tests (tokenizer, trim, etc.) ────────────────────
    # Build parser functions as a library so tests can link against them
    add_library(shell_lib STATIC ${SHELL_SOURCES})
    target_link_libraries(shell_lib PRIVATE ${READLINE_LIB})
    target_include_directories(shell_lib PRIVATE ${READLINE_INCLUDE})
    # Exclude main() from the library
    target_compile_definitions(shell_lib PRIVATE TESTING_BUILD)

    add_executable(test_tokenizer tests/test_tokenizer.cpp)
    target_include_directories(test_tokenizer PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(test_tokenizer GTest::gtest_main shell_lib)
    add_test(NAME TokenizerTests COMMAND test_tokenizer)

    # ── Integration tests ─────────────────────────────────────
    add_executable(test_integration
        tests/test_integration.cpp
        tests/test_basic.cpp
        tests/test_redirection.cpp
        tests/test_background.cpp
        tests/test_cd.cpp
        tests/test_pipes.cpp
        tests/test_history.cpp
        tests/test_env_vars.cpp
        tests/test_glob.cpp
        tests/test_operators.cpp
        tests/test_tashrc.cpp
        tests/test_alias.cpp
    )
    target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(test_integration GTest::gtest)
    add_test(NAME IntegrationTests COMMAND test_integration)
    set_tests_properties(IntegrationTests PROPERTIES
        ENVIRONMENT "TASH_SHELL_BIN=$<TARGET_FILE:shell.out>"
    )
endif()
