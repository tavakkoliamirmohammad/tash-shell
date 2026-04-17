// 3+-stage pipelines that mix builtins and external commands. The
// existing test_pipes.cpp covers 2-3 stage external-only pipes; this
// fills the gap the review flagged.

#include "test_helpers.h"

TEST(MixedPipelines, ExternalBuiltinExternal) {
    // external → builtin (history) → external
    auto r = run_shell(
        "echo probe1\n"
        "echo probe2\n"
        "history | grep probe | head -5\n"
        "exit\n");
    // Both probes should appear at least once (from their own execution)
    // and possibly through history; the key assertion is the pipeline
    // didn't error out.
    EXPECT_EQ(r.output.find("history: No such file or directory"),
              std::string::npos);
    EXPECT_EQ(r.output.find("grep: No such file or directory"),
              std::string::npos);
}

TEST(MixedPipelines, BuiltinExternalBuiltin) {
    // builtin (alias) → external (grep) → builtin (linkify)
    auto r = run_shell(
        "alias probe1='echo first'\n"
        "alias probe2='echo second'\n"
        "alias | grep probe | linkify\n"
        "exit\n");
    EXPECT_NE(r.output.find("probe1"), std::string::npos);
    EXPECT_NE(r.output.find("probe2"), std::string::npos);
    EXPECT_EQ(r.output.find("linkify: No such file or directory"),
              std::string::npos);
}

TEST(MixedPipelines, FourStagePipeline) {
    auto r = run_shell(
        "echo hello | tr a-z A-Z | rev | cat\n"
        "exit\n");
    // hello → HELLO → OLLEH
    EXPECT_NE(r.output.find("OLLEH"), std::string::npos);
}

TEST(MixedPipelines, PipeIntoCopyBuiltin) {
    // external → builtin
    auto r = run_shell(
        "echo clipboard_payload_xyz | copy\n"
        "exit\n");
    // copy emits the OSC 52 prefix; never the command-not-found path.
    EXPECT_NE(r.output.find("\x1b]52;c;"), std::string::npos);
}

TEST(MixedPipelines, PipeIntoTableBuiltin) {
    auto r = run_shell(
        "printf 'A B\\n1 2\\n3 4\\n' | table\n"
        "exit\n");
    EXPECT_NE(r.output.find("\xe2\x94\x8c"), std::string::npos);  // ┌
}

TEST(MixedPipelines, AliasPassedThroughPipeline) {
    // Ensure aliases still resolve inside a 3-stage pipeline.
    auto r = run_shell(
        "alias me='echo pipeline_alias_works'\n"
        "me | cat | head -1\n"
        "exit\n");
    EXPECT_NE(r.output.find("pipeline_alias_works"),
              std::string::npos);
}

TEST(MixedPipelines, RedirectionAtEndOfMixedPipe) {
    std::string out = "/tmp/tash_mixed_pipe_" + std::to_string(getpid());
    run_shell(
        "alias marker='echo redir_pipe_payload'\n"
        "marker | cat > " + out + "\n"
        "exit\n");
    std::string content = read_file(out);
    unlink(out.c_str());
    EXPECT_NE(content.find("redir_pipe_payload"), std::string::npos);
}
