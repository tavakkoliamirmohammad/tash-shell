cmake_minimum_required(VERSION 3.17)
project(tash VERSION 1.0.0 LANGUAGES CXX)

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
)
set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(replxx)

# ── Source files shared by shell binary and test library ─────────
set(SHELL_SOURCES
    main.cpp
    colors.cpp
    prompt.cpp
    parser.cpp
    builtins.cpp
    process.cpp
    completion.cpp
    suggest.cpp
    history.cpp
    frecency.cpp
    highlight.cpp
)

# ── Main shell binary ─────────────────────────────────────────
add_executable(tash.out ${SHELL_SOURCES})
target_link_libraries(tash.out PRIVATE replxx)
target_include_directories(tash.out PRIVATE ${replxx_SOURCE_DIR}/include)
target_compile_options(tash.out PRIVATE -Wall -Wextra)

# ── Testing ───────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # ── Unit tests (tokenizer, trim, etc.) ────────────────────
    add_library(shell_lib STATIC ${SHELL_SOURCES})
    target_link_libraries(shell_lib PRIVATE replxx)
    target_include_directories(shell_lib PRIVATE ${replxx_SOURCE_DIR}/include)
    target_compile_definitions(shell_lib PRIVATE TESTING_BUILD)

    add_executable(test_tokenizer tests/test_tokenizer.cpp)
    target_include_directories(test_tokenizer PRIVATE ${CMAKE_SOURCE_DIR} ${replxx_SOURCE_DIR}/include)
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
        tests/test_script.cpp
        tests/test_comments.cpp
        tests/test_modern_ux.cpp
    )
    target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests ${replxx_SOURCE_DIR}/include)
    target_link_libraries(test_integration GTest::gtest)
    add_test(NAME IntegrationTests COMMAND test_integration)
    set_tests_properties(IntegrationTests PROPERTIES
        ENVIRONMENT "TASH_SHELL_BIN=$<TARGET_FILE:tash.out>"
    )
endif()
