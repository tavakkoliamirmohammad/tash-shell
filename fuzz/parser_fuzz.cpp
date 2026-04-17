// libFuzzer harness for the parsing surface.
//
// Feeds arbitrary bytes into the three entry points that PR #94 found
// latent bugs in: parse_command_line (splitter), parse_redirections
// (quote/escape + heredoc markers), and scan_pending_heredocs. Any
// crash, ASan hit, or UBSan violation here is a real parser bug.
//
// Build: cmake -S . -B build-fuzz -DTASH_ENABLE_FUZZER=ON \
//              -DCMAKE_CXX_COMPILER=clang++
//        cmake --build build-fuzz --target tash_parser_fuzzer
// Run:   ./build-fuzz/tash_parser_fuzzer fuzz/corpus -max_total_time=60

#include "tash/core.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // libFuzzer invokes this over and over; stay cheap and pure.
    std::string input(reinterpret_cast<const char *>(data), size);

    // 1. Segment splitter.
    auto segments = parse_command_line(input);

    // 2. Redirection parse on each segment.
    for (auto &seg : segments) {
        (void)parse_redirections(seg.command);
    }

    // 3. Heredoc marker scan on the full input.
    (void)scan_pending_heredocs(input);

    // 4. Input-completeness predicate.
    (void)is_input_complete(input);

    return 0;
}
