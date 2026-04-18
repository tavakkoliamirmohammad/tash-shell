# Build-flag toggles: sanitizers, coverage, and static linking.
#
# All three apply via add_compile_options/add_link_options so every
# target — tash.out, shell_lib, test_*, tash_parser_fuzzer — picks up
# the flags without having to opt in individually.

# ── Sanitizers (ASan + UBSan) ─────────────────────────────────
# Opt-in instrumentation for CI and local debugging. Let CMAKE_BUILD_TYPE
# drive -O/-g; RelWithDebInfo gives readable stack traces without
# killing speed.
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

# ── Static linking (musl / Alpine) ────────────────────────────
# Produces a fully-static tash binary. Realistically only works on
# Linux with musl — glibc's NSS pulls in libc internals at runtime
# that a `-static` link can't resolve cleanly, and macOS has no
# static libSystem at all. Kept permissive (warn, don't abort) so a
# developer on macOS/glibc can still attempt a config, and so CI's
# config-only smoke tests don't need to special-case the option.
option(TASH_STATIC "Link the tash binary statically (requires static libs for deps)" OFF)
if(TASH_STATIC)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING "TASH_STATIC is only well-tested on Linux/musl (Alpine). Proceeding anyway.")
    endif()
    message(STATUS "Static linking enabled (-static)")
    # .a-first search so find_library prefers static. Fall back to shared
    # if no static variant is installed.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.so;.dylib")
    add_link_options(-static)
endif()
