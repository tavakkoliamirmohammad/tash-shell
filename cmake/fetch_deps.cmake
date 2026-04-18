# Third-party dependencies fetched on-demand via FetchContent.
#
# replxx — always fetched (line editor, no system package we trust).
# nlohmann_json — fetched only when features.cmake couldn't find a
#                 system package (TASH_NEED_FETCH_JSON).

include(FetchContent)

# ── replxx (replaces readline/libedit) ────────────────────────
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

# ── nlohmann_json fallback fetch ──────────────────────────────
if(TASH_NEED_FETCH_JSON)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()
