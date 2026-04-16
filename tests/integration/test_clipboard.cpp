#include "test_helpers.h"

// When the system clipboard isn't accessible, copy_to_clipboard falls back to
// an OSC 52 escape sequence emitted to the terminal. Either way we should see
// either the OSC sequence or a successful no-output path, and the builtin
// must exist in the dispatch table (no "command not found").
TEST(Clipboard, CopyBuiltinExists) {
    auto r = run_shell("copy hello_from_tash\nexit\n");
    EXPECT_EQ(r.output.find("copy: command not found"), std::string::npos);
    EXPECT_EQ(r.output.find("No such file or directory"), std::string::npos);
}

TEST(Clipboard, PasteBuiltinExists) {
    auto r = run_shell("paste\nexit\n");
    EXPECT_EQ(r.output.find("paste: command not found"), std::string::npos);
    EXPECT_EQ(r.output.find("No such file or directory"), std::string::npos);
}

// Builtins like `copy` must also work when used as the final stage of a
// pipeline (`echo foo | copy`). Before the pipeline-builtin fix this
// reported "copy: No such file or directory".
TEST(Clipboard, BuiltinWorksInPipeline) {
    auto r = run_shell("echo amir | copy\nexit\n");
    EXPECT_EQ(r.output.find("copy: No such file or directory"),
              std::string::npos);
    EXPECT_EQ(r.output.find("copy: command not found"),
              std::string::npos);
}
