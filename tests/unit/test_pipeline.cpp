#include <gtest/gtest.h>


#include "tash/core/structured_pipe.h"

using namespace tash::structured_pipe;
using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════
// Input parsing
// ═══════════════════════════════════════════════════════════════

TEST(ParseJsonInput, ValidJsonArrayParsedCorrectly) {
    std::string input = R"([{"name":"alice","age":30},{"name":"bob","age":25}])";
    JsonValue result = parse_input(input);
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0]["name"], "alice");
    EXPECT_EQ(result[0]["age"], 30);
    EXPECT_EQ(result[1]["name"], "bob");
}

TEST(ParseJsonObject, SingleObjectWrappedInArray) {
    std::string input = R"({"name":"alice","age":30})";
    JsonValue result = parse_input(input);
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "alice");
    EXPECT_EQ(result[0]["age"], 30);
}

TEST(ParseTextInput, PlainTextLinesBecomArrayOfLineObjects) {
    std::string input = "hello world\nfoo bar\nbaz";
    JsonValue result = parse_input(input);
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["line"], "hello world");
    EXPECT_EQ(result[0]["index"], 0);
    EXPECT_EQ(result[1]["line"], "foo bar");
    EXPECT_EQ(result[1]["index"], 1);
    EXPECT_EQ(result[2]["line"], "baz");
    EXPECT_EQ(result[2]["index"], 2);
}

// ═══════════════════════════════════════════════════════════════
// Where filter
// ═══════════════════════════════════════════════════════════════

TEST(WhereFilterNumeric, FiltersOnNumericComparison) {
    JsonValue input = json::parse(R"([
        {"name":"alice","age":25},
        {"name":"bob","age":17},
        {"name":"carol","age":30}
    ])");
    JsonValue result = op_where({"age", ">", "18"}, input);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0]["name"], "alice");
    EXPECT_EQ(result[1]["name"], "carol");
}

TEST(WhereFilterString, FiltersOnStringEquality) {
    JsonValue input = json::parse(R"([
        {"name":"alice","role":"admin"},
        {"name":"bob","role":"user"},
        {"name":"carol","role":"admin"}
    ])");
    JsonValue result = op_where({"name", "==", "\"alice\""}, input);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "alice");
}

TEST(WhereFilterNotEqual, NotEqualOperatorWorks) {
    JsonValue input = json::parse(R"([
        {"name":"alice","status":"active"},
        {"name":"bob","status":"inactive"},
        {"name":"carol","status":"active"}
    ])");
    JsonValue result = op_where({"status", "!=", "\"active\""}, input);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "bob");
}

// ═══════════════════════════════════════════════════════════════
// Sort
// ═══════════════════════════════════════════════════════════════

TEST(SortByAscending, SortsAlphabetically) {
    JsonValue input = json::parse(R"([
        {"name":"carol"},
        {"name":"alice"},
        {"name":"bob"}
    ])");
    JsonValue result = op_sort_by({"name"}, input);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["name"], "alice");
    EXPECT_EQ(result[1]["name"], "bob");
    EXPECT_EQ(result[2]["name"], "carol");
}

TEST(SortByDescending, SortsReverseAlphabetically) {
    JsonValue input = json::parse(R"([
        {"name":"alice"},
        {"name":"carol"},
        {"name":"bob"}
    ])");
    JsonValue result = op_sort_by({"name", "--desc"}, input);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["name"], "carol");
    EXPECT_EQ(result[1]["name"], "bob");
    EXPECT_EQ(result[2]["name"], "alice");
}

TEST(SortByNumeric, SortsNumerically) {
    JsonValue input = json::parse(R"([
        {"name":"b","age":30},
        {"name":"a","age":10},
        {"name":"c","age":20}
    ])");
    JsonValue result = op_sort_by({"age"}, input);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["age"], 10);
    EXPECT_EQ(result[1]["age"], 20);
    EXPECT_EQ(result[2]["age"], 30);
}

// ═══════════════════════════════════════════════════════════════
// Select / Reject
// ═══════════════════════════════════════════════════════════════

TEST(SelectColumns, KeepsOnlyNamedFields) {
    JsonValue input = json::parse(R"([
        {"name":"alice","age":30,"role":"admin"},
        {"name":"bob","age":25,"role":"user"}
    ])");
    JsonValue result = op_select({"name", "age"}, input);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_TRUE(result[0].contains("name"));
    EXPECT_TRUE(result[0].contains("age"));
    EXPECT_FALSE(result[0].contains("role"));
}

TEST(RejectColumns, RemovesNamedFields) {
    JsonValue input = json::parse(R"([
        {"name":"alice","password":"secret","role":"admin"},
        {"name":"bob","password":"hunter2","role":"user"}
    ])");
    JsonValue result = op_reject({"password"}, input);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_FALSE(result[0].contains("password"));
    EXPECT_TRUE(result[0].contains("name"));
    EXPECT_TRUE(result[0].contains("role"));
}

// ═══════════════════════════════════════════════════════════════
// First / Last
// ═══════════════════════════════════════════════════════════════

TEST(FirstN, ReturnsTopN) {
    JsonValue input = json::parse(R"([{"v":1},{"v":2},{"v":3},{"v":4},{"v":5}])");
    JsonValue result = op_first({"3"}, input);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["v"], 1);
    EXPECT_EQ(result[2]["v"], 3);
}

TEST(LastN, ReturnsBottomN) {
    JsonValue input = json::parse(R"([{"v":1},{"v":2},{"v":3},{"v":4},{"v":5}])");
    JsonValue result = op_last({"2"}, input);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0]["v"], 4);
    EXPECT_EQ(result[1]["v"], 5);
}

// ═══════════════════════════════════════════════════════════════
// Count
// ═══════════════════════════════════════════════════════════════

TEST(Count, ReturnsCountObject) {
    JsonValue input = json::parse(R"([{"a":1},{"a":2},{"a":3}])");
    JsonValue result = op_count({}, input);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["count"], 3);
}

// ═══════════════════════════════════════════════════════════════
// Uniq
// ═══════════════════════════════════════════════════════════════

TEST(UniqRemovesDupes, ConsecutiveIdenticalObjectsRemoved) {
    JsonValue input = json::parse(R"([{"v":1},{"v":1},{"v":2},{"v":2},{"v":1}])");
    JsonValue result = op_uniq({}, input);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["v"], 1);
    EXPECT_EQ(result[1]["v"], 2);
    EXPECT_EQ(result[2]["v"], 1);
}

// ═══════════════════════════════════════════════════════════════
// Output formatters
// ═══════════════════════════════════════════════════════════════

TEST(ToJson, OutputsValidJson) {
    JsonValue input = json::parse(R"([{"name":"alice","age":30}])");
    std::string output = to_json(input);
    // Should be parseable
    JsonValue reparsed = json::parse(output);
    EXPECT_EQ(reparsed, input);
}

TEST(ToCsv, OutputsCsvWithHeaders) {
    JsonValue input = json::parse(R"([{"name":"alice","age":30},{"name":"bob","age":25}])");
    std::string output = to_csv(input);
    // Should contain header and data
    EXPECT_NE(output.find("name"), std::string::npos);
    EXPECT_NE(output.find("age"), std::string::npos);
    EXPECT_NE(output.find("alice"), std::string::npos);
    EXPECT_NE(output.find("bob"), std::string::npos);
    // Count lines: header + 2 data rows
    size_t line_count = 0;
    for (char c : output) { if (c == '\n') ++line_count; }
    EXPECT_EQ(line_count, 3u);
}

// ═══════════════════════════════════════════════════════════════
// Table rendering
// ═══════════════════════════════════════════════════════════════

TEST(RenderTableBasic, TableHasBoxDrawingChars) {
    JsonValue input = json::parse(R"([{"name":"alice","size":1024}])");
    std::string output = render_table(input);
    // Should contain box-drawing chars
    EXPECT_NE(output.find("\xe2\x94\x8c"), std::string::npos); // ┌
    EXPECT_NE(output.find("\xe2\x94\x90"), std::string::npos); // ┐
    EXPECT_NE(output.find("\xe2\x94\x94"), std::string::npos); // └
    EXPECT_NE(output.find("\xe2\x94\x98"), std::string::npos); // ┘
    EXPECT_NE(output.find("\xe2\x94\x82"), std::string::npos); // │
    EXPECT_NE(output.find("alice"), std::string::npos);
    EXPECT_NE(output.find("1024"), std::string::npos);
}

TEST(RenderTableEmpty, EmptyDataRendersEmptyTable) {
    JsonValue input = json::parse("[]");
    std::string output = render_table(input);
    EXPECT_TRUE(output.empty());
}

// ═══════════════════════════════════════════════════════════════
// Chained operators
// ═══════════════════════════════════════════════════════════════

TEST(ChainedOperators, WhereSortByFirstWorksTogether) {
    JsonValue input = json::parse(R"([
        {"name":"alice","age":25},
        {"name":"bob","age":17},
        {"name":"carol","age":30},
        {"name":"dave","age":22}
    ])");

    // where age > 18 |> sort-by age |> first 2
    JsonValue step1 = op_where({"age", ">", "18"}, input);
    ASSERT_EQ(step1.size(), 3u); // alice(25), carol(30), dave(22)

    JsonValue step2 = op_sort_by({"age"}, step1);
    ASSERT_EQ(step2.size(), 3u);
    EXPECT_EQ(step2[0]["name"], "dave");  // 22
    EXPECT_EQ(step2[1]["name"], "alice"); // 25
    EXPECT_EQ(step2[2]["name"], "carol"); // 30

    JsonValue step3 = op_first({"2"}, step2);
    ASSERT_EQ(step3.size(), 2u);
    EXPECT_EQ(step3[0]["name"], "dave");
    EXPECT_EQ(step3[1]["name"], "alice");
}

// ═══════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════

TEST(EmptyInput, EmptyArrayThroughOperatorsNoCrash) {
    JsonValue input = json::parse("[]");
    EXPECT_EQ(op_where({"a", ">", "1"}, input).size(), 0u);
    EXPECT_EQ(op_sort_by({"a"}, input).size(), 0u);
    EXPECT_EQ(op_select({"a"}, input).size(), 0u);
    EXPECT_EQ(op_reject({"a"}, input).size(), 0u);
    EXPECT_EQ(op_first({"3"}, input).size(), 0u);
    EXPECT_EQ(op_last({"3"}, input).size(), 0u);
    ASSERT_EQ(op_count({}, input).size(), 1u);
    EXPECT_EQ(op_count({}, input)[0]["count"], 0);
    EXPECT_EQ(op_uniq({}, input).size(), 0u);
}

TEST(SingleItemInput, OneElementArrayWorks) {
    JsonValue input = json::parse(R"([{"name":"alice","age":30}])");
    EXPECT_EQ(op_where({"age", ">", "18"}, input).size(), 1u);
    EXPECT_EQ(op_sort_by({"name"}, input).size(), 1u);
    EXPECT_EQ(op_first({"5"}, input).size(), 1u);
    EXPECT_EQ(op_last({"5"}, input).size(), 1u);
    EXPECT_EQ(op_count({}, input)[0]["count"], 1);
    EXPECT_EQ(op_uniq({}, input).size(), 1u);
}

TEST(MissingField, WhereAndSortByOnNonexistentFieldGraceful) {
    JsonValue input = json::parse(R"([
        {"name":"alice","age":30},
        {"name":"bob"}
    ])");
    // where on missing field: items without the field are skipped
    JsonValue result = op_where({"age", ">", "18"}, input);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "alice");

    // sort-by on partially missing field: items without field go to end
    JsonValue sorted = op_sort_by({"age"}, input);
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0]["name"], "alice");
    EXPECT_EQ(sorted[1]["name"], "bob");
}

TEST(InvalidOperator, UnknownOperatorReturnsError) {
    JsonValue input = json::parse(R"([{"a":1}])");
    JsonValue result = apply_operator("foobar", {}, input);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0].contains("error"));
    EXPECT_NE(result[0]["error"].get<std::string>().find("unknown"), std::string::npos);
}

TEST(ParseOperatorArgs, WhereOperatorParsedCorrectly) {
    ParsedOperator op = parse_operator("where size > 100");
    EXPECT_EQ(op.name, "where");
    ASSERT_EQ(op.args.size(), 3u);
    EXPECT_EQ(op.args[0], "size");
    EXPECT_EQ(op.args[1], ">");
    EXPECT_EQ(op.args[2], "100");
}

// ═══════════════════════════════════════════════════════════════
// Pipeline detection and splitting
// ═══════════════════════════════════════════════════════════════

TEST(PipelineParsing, HasStructuredPipeDetection) {
    EXPECT_TRUE(has_structured_pipe("ls |> where name == foo"));
    EXPECT_FALSE(has_structured_pipe("ls | grep foo"));
    EXPECT_FALSE(has_structured_pipe("echo 'hello |> world'"));
}

TEST(PipelineParsing, SplitPipelineSegments) {
    auto segments = split_pipeline("ls -la |> where size > 100 |> sort-by name");
    ASSERT_EQ(segments.size(), 3u);
    EXPECT_EQ(segments[0].text, "ls -la");
    EXPECT_TRUE(segments[0].is_command);
    EXPECT_EQ(segments[1].text, "where size > 100");
    EXPECT_FALSE(segments[1].is_command);
    EXPECT_EQ(segments[2].text, "sort-by name");
    EXPECT_FALSE(segments[2].is_command);
}

