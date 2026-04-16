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
    src/plugins/plugin_registry.cpp
    src/plugins/fish_completion_provider.cpp
    src/plugins/fig_completion_provider.cpp
)

# ── AI source files (conditional) ────────────────────────────
if(TASH_AI_ENABLED)
    list(APPEND SHELL_SOURCES
        src/ai/ai_handler.cpp
        src/ai/llm_client.cpp
        src/ai/ai_config.cpp
        src/ai/context_suggest.cpp
        src/plugins/ai_error_hook_provider.cpp
    )
endif()

# ── SQLite history source files (conditional) ────────────────
if(TASH_SQLITE_ENABLED)
    list(APPEND SHELL_SOURCES
        src/plugins/sqlite_history_provider.cpp
    )
endif()

# ── Generate man page with version ────────────────────────────
configure_file(tash.1.in tash.1 @ONLY)

# ── Main shell binary ─────────────────────────────────────────
add_executable(tash.out ${SHELL_SOURCES})
target_link_libraries(tash.out PRIVATE replxx)
target_include_directories(tash.out PRIVATE ${TASH_INCLUDE_DIRS})
target_include_directories(tash.out PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_compile_options(tash.out PRIVATE -Wall -Wextra)
target_compile_definitions(tash.out PRIVATE TASH_VERSION_STRING="${PROJECT_VERSION}")

if(TASH_AI_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_AI_ENABLED)
    target_link_libraries(tash.out PRIVATE OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Threads::Threads)
    target_include_directories(tash.out PRIVATE ${httplib_SOURCE_DIR})
endif()

if(TASH_SQLITE_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_SQLITE_ENABLED)
    target_include_directories(tash.out PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(tash.out PRIVATE ${SQLite3_LIBRARIES})
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
    target_include_directories(shell_lib PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
    target_compile_definitions(shell_lib PRIVATE TESTING_BUILD TASH_VERSION_STRING="${PROJECT_VERSION}")

    if(TASH_AI_ENABLED)
        target_compile_definitions(shell_lib PRIVATE TASH_AI_ENABLED)
        target_link_libraries(shell_lib PRIVATE OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Threads::Threads)
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

    add_executable(test_ai tests/unit/test_ai.cpp)
    target_include_directories(test_ai PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_ai GTest::gtest_main shell_lib)
    if(TASH_AI_ENABLED)
        target_compile_definitions(test_ai PRIVATE TASH_AI_ENABLED)
        target_include_directories(test_ai PRIVATE ${httplib_SOURCE_DIR})
        target_include_directories(test_ai PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
        target_link_libraries(test_ai CURL::libcurl)
    endif()
    gtest_discover_tests(test_ai TEST_PREFIX "unit/ai/")

    # ── Plugin registry tests ────────────────────────────────
    add_executable(test_plugin_registry tests/unit/test_plugin_registry.cpp)
    target_include_directories(test_plugin_registry PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_plugin_registry GTest::gtest_main shell_lib)
    gtest_discover_tests(test_plugin_registry TEST_PREFIX "unit/plugins/")

    # ── Fish completion provider tests ────────────────────────
    add_executable(test_fish_completion tests/unit/test_fish_completion.cpp)
    target_include_directories(test_fish_completion PRIVATE ${TASH_INCLUDE_DIRS})
    target_include_directories(test_fish_completion PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
    target_link_libraries(test_fish_completion GTest::gtest_main shell_lib)
    gtest_discover_tests(test_fish_completion TEST_PREFIX "unit/fish/")

    # ── Fig completion provider tests ─────────────────────────
    add_executable(test_fig_completion tests/unit/test_fig_completion.cpp)
    target_include_directories(test_fig_completion PRIVATE ${TASH_INCLUDE_DIRS})
    target_include_directories(test_fig_completion PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
    target_link_libraries(test_fig_completion GTest::gtest_main shell_lib)
    if(TASH_AI_ENABLED)
        target_compile_definitions(test_fig_completion PRIVATE TASH_AI_ENABLED)
        target_include_directories(test_fig_completion PRIVATE ${httplib_SOURCE_DIR})
    endif()
    gtest_discover_tests(test_fig_completion TEST_PREFIX "unit/fig/")

    # ── SQLite history tests (conditional) ────────────────────
    if(TASH_SQLITE_ENABLED)
        add_executable(test_sqlite_history tests/unit/test_sqlite_history.cpp
            src/plugins/sqlite_history_provider.cpp)
        target_include_directories(test_sqlite_history PRIVATE ${TASH_INCLUDE_DIRS} ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(test_sqlite_history GTest::gtest_main ${SQLite3_LIBRARIES})
        gtest_discover_tests(test_sqlite_history TEST_PREFIX "unit/sqlite/")
    endif()
    # ── AI error hook tests ──────────────────────────────────
    add_executable(test_ai_error_hook tests/unit/test_ai_error_hook.cpp)
    target_include_directories(test_ai_error_hook PRIVATE ${TASH_INCLUDE_DIRS})
    target_link_libraries(test_ai_error_hook GTest::gtest_main shell_lib)
    if(TASH_AI_ENABLED)
        target_compile_definitions(test_ai_error_hook PRIVATE TASH_AI_ENABLED)
        target_include_directories(test_ai_error_hook PRIVATE ${httplib_SOURCE_DIR})
        target_include_directories(test_ai_error_hook PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
        target_link_libraries(test_ai_error_hook CURL::libcurl)
    endif()
    gtest_discover_tests(test_ai_error_hook TEST_PREFIX "unit/plugins/ai/")

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
