#include "test_helpers.h"

// `copy` + `paste` should dispatch as builtins. The strongest portable
// signal that the builtin actually ran is the OSC 52 escape sequence
// emitted by copy_to_clipboard before trying pbcopy/xclip/wl-copy. That
// sequence starts with \x1b]52;c; and contains a base64 payload.
//
// We avoid negative assertions like
//   EXPECT_EQ(output.find("copy: command not found"), npos)
// because some CI containers (observed on Fedora) have
// command-not-found packages that emit that exact string for *unrelated*
// child-process failures (e.g. the clipboard fallback trying xclip/xsel
// when those aren't installed), producing false failures.

TEST(Clipboard, CopyEmitsOsc52) {
    auto r = run_shell("copy hello_from_tash\nexit\n");
    // OSC 52 prefix ("\x1b]52;c;") is always emitted, even when a system
    // clipboard tool also succeeds.
    EXPECT_NE(r.output.find("\x1b]52;c;"), std::string::npos);
}

TEST(Clipboard, PasteDoesNotCrash) {
    // Exit-status-like signal: the shell must reach the `exit` builtin
    // and print its farewell, which means `paste` returned cleanly.
    auto r = run_shell("paste\nexit\n");
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
}

// Piped-to-builtin: `echo foo | copy` must reach the copy builtin and
// emit its OSC 52 sequence with the piped payload.
TEST(Clipboard, BuiltinWorksInPipeline) {
    auto r = run_shell("echo amir | copy\nexit\n");
    EXPECT_NE(r.output.find("\x1b]52;c;"), std::string::npos);
}
