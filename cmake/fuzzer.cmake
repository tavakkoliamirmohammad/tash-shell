# libFuzzer harness (clang-only, opt-in).
#
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
        src/util/io.cpp
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
