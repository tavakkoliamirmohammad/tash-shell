cmake_minimum_required(VERSION 3.17)
project(tash VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cohesive, focused modules. Order matters:
#   features      — default build type + find_package() for system deps.
#   fetch_deps    — FetchContent for replxx (always) + nlohmann_json
#                   fallback when features.cmake didn't find a system copy.
#   sanitizers    — TASH_SANITIZERS / TASH_COVERAGE / TASH_STATIC toggles.
include(cmake/features.cmake)
include(cmake/fetch_deps.cmake)
include(cmake/sanitizers.cmake)

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
    src/ai/model_defaults.cpp
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
    src/builtins/meta.cpp
    src/builtins/config.cpp
    src/builtins/trap.cpp
    src/ui/prompt.cpp
    src/ui/highlight.cpp
    src/ui/completion.cpp
    src/ui/suggest.cpp
    src/history/history.cpp
    src/history/frecency.cpp
    src/util/colors.cpp
    src/util/config_file.cpp
    src/util/config_resolver.cpp
    src/util/crash_dump.cpp
    src/util/cwd.cpp
    src/util/io.cpp
    src/util/safe_exec.cpp
    src/util/safe_tmpdir.cpp
    src/util/theme.cpp
)

# ── Plugin registry + list ────────────────────────────────────
# Each plugin registers itself in cmake/plugin_list.cmake. Adding a new
# plugin means appending one tash_register_plugin(...) call at the bottom
# of that file, which avoids per-PR merge conflicts.
include(${CMAKE_SOURCE_DIR}/cmake/plugins.cmake)
# Cluster subsystem (TASH_CLUSTER option, toml++). Must come before
# plugin_list.cmake so its REQUIRES TASH_CLUSTER_ENABLED gate works.
include(${CMAKE_SOURCE_DIR}/cmake/cluster.cmake)
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

# Embed data/ai_models.json into the binary as a raw string literal.
# Single source of truth — bumping the JSON regenerates the embedded
# copy at build time. The env-var TASH_AI_MODELS_JSON still takes
# precedence for runtime overrides (tests, ops).
file(READ "${CMAKE_SOURCE_DIR}/data/ai_models.json" TASH_AI_MODELS_JSON_CONTENT)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/ai_models_embedded.cpp.in"
    "${CMAKE_BINARY_DIR}/generated/ai_models_embedded.cpp"
    @ONLY)
target_sources(tash.out PRIVATE "${CMAKE_BINARY_DIR}/generated/ai_models_embedded.cpp")
target_include_directories(tash.out PRIVATE "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(tash.out PRIVATE CURL::libcurl Threads::Threads)

if(TASH_SQLITE_ENABLED)
    target_compile_definitions(tash.out PRIVATE TASH_SQLITE_ENABLED)
    target_include_directories(tash.out PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(tash.out PRIVATE ${SQLite3_LIBRARIES})
endif()

include(cmake/tests.cmake)
include(cmake/fuzzer.cmake)
