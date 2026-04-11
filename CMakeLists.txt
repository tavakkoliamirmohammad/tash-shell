cmake_minimum_required(VERSION 3.17)
project(amish VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Main shell binary ─────────────────────────────────────────
add_executable(shell.out main.cpp colors.cpp)
target_link_libraries(shell.out -lreadline)

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
    add_library(shell_lib STATIC
        main.cpp
        colors.cpp
    )
    target_link_libraries(shell_lib -lreadline)
    # Exclude main() from the library
    target_compile_definitions(shell_lib PRIVATE TESTING_BUILD)

    add_executable(test_tokenizer tests/test_tokenizer.cpp)
    target_include_directories(test_tokenizer PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(test_tokenizer GTest::gtest_main shell_lib)
    add_test(NAME TokenizerTests COMMAND test_tokenizer)

    # ── Integration tests ─────────────────────────────────────
    add_executable(test_integration tests/test_integration.cpp)
    target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(test_integration GTest::gtest)
    # Pass shell binary path to integration tests
    add_test(NAME IntegrationTests COMMAND test_integration)
    set_tests_properties(IntegrationTests PROPERTIES
        ENVIRONMENT "AMISH_SHELL_BIN=$<TARGET_FILE:shell.out>"
    )
endif()
