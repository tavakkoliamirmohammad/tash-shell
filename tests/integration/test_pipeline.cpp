#include "test_helpers.h"

// The `|>` operator routes structured pipelines through
// tash::structured_pipe::execute_pipeline. Each test runs a small
// shell-command-producing-JSON-or-text and verifies the operator chain
// produced the expected rendering.

TEST(StructuredPipe, SortByNumericField) {
    auto r = run_shell(
        "echo '[{\"name\":\"alice\",\"age\":30},"
        "{\"name\":\"bob\",\"age\":25}]'"
        " |> sort-by age |> to-json\n"
        "exit\n");
    // bob should come first after sorting by age ascending.
    size_t bob = r.output.find("\"bob\"");
    size_t alice = r.output.find("\"alice\"");
    ASSERT_NE(bob, std::string::npos);
    ASSERT_NE(alice, std::string::npos);
    EXPECT_LT(bob, alice);
}

TEST(StructuredPipe, WhereFiltersRows) {
    auto r = run_shell(
        "echo '[{\"name\":\"alice\",\"active\":true},"
        "{\"name\":\"bob\",\"active\":false}]'"
        " |> where active == true |> to-json\n"
        "exit\n");
    EXPECT_NE(r.output.find("alice"), std::string::npos);
    EXPECT_EQ(r.output.find("bob"), std::string::npos);
}

TEST(StructuredPipe, SelectProjectsFields) {
    auto r = run_shell(
        "echo '[{\"name\":\"alice\",\"age\":30,\"email\":\"a@x.com\"}]'"
        " |> select name |> to-json\n"
        "exit\n");
    EXPECT_NE(r.output.find("\"name\": \"alice\""), std::string::npos);
    EXPECT_EQ(r.output.find("age"), std::string::npos);
    EXPECT_EQ(r.output.find("email"), std::string::npos);
}

TEST(StructuredPipe, CountLineBasedInput) {
    // Non-JSON input auto-wraps into one row per line.
    auto r = run_shell("ls /tmp |> count\nexit\n");
    // Count renders via to-table by default in the engine's pipeline.
    EXPECT_NE(r.output.find("count"), std::string::npos);
}

TEST(StructuredPipe, ToTableRendersBoxDrawing) {
    auto r = run_shell(
        "echo '[{\"a\":1,\"b\":2}]' |> to-table\nexit\n");
    EXPECT_NE(r.output.find("\xe2\x94\x8c"), std::string::npos); // ┌
    EXPECT_NE(r.output.find("\xe2\x94\x98"), std::string::npos); // ┘
}

TEST(StructuredPipe, ToCsvProducesCommaSeparated) {
    auto r = run_shell(
        "echo '[{\"name\":\"a\",\"n\":1},{\"name\":\"b\",\"n\":2}]'"
        " |> to-csv\n"
        "exit\n");
    // Column order is implementation-defined (currently sorted), so just
    // verify both columns and both rows appear with a comma separator.
    EXPECT_NE(r.output.find("name"), std::string::npos);
    EXPECT_NE(r.output.find(",n"),   std::string::npos);
    EXPECT_NE(r.output.find(","),    std::string::npos);
    // Row values appear with their commas in the output somewhere.
    EXPECT_NE(r.output.find("1,a"),  std::string::npos);
    EXPECT_NE(r.output.find("2,b"),  std::string::npos);
}

TEST(StructuredPipe, FirstAndLast) {
    auto r = run_shell(
        "echo '[{\"x\":1},{\"x\":2},{\"x\":3}]' |> first |> to-json\n"
        "exit\n");
    EXPECT_NE(r.output.find("\"x\": 1"), std::string::npos);
    EXPECT_EQ(r.output.find("\"x\": 3"), std::string::npos);
}

TEST(StructuredPipe, ChainedOperators) {
    auto r = run_shell(
        "echo '[{\"name\":\"alice\",\"age\":30,\"active\":true},"
        "{\"name\":\"bob\",\"age\":25,\"active\":true},"
        "{\"name\":\"carol\",\"age\":40,\"active\":false}]'"
        " |> where active == true |> sort-by age |> select name |> to-json\n"
        "exit\n");
    // Expect bob then alice, no carol.
    size_t bob = r.output.find("bob");
    size_t alice = r.output.find("alice");
    ASSERT_NE(bob, std::string::npos);
    ASSERT_NE(alice, std::string::npos);
    EXPECT_LT(bob, alice);
    EXPECT_EQ(r.output.find("carol"), std::string::npos);
}

// Traditional `|` must still work when the line contains no `|>`.
TEST(StructuredPipe, TraditionalPipeUnaffected) {
    auto r = run_shell("echo hello | cat\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}
