#ifndef TASH_CORE_STRUCTURED_PIPE_H
#define TASH_CORE_STRUCTURED_PIPE_H

#ifdef TASH_AI_ENABLED

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace tash {
namespace structured_pipe {

using JsonValue = nlohmann::json;

// ── Pipeline parsing ──────────────────────────────────────────

struct PipelineSegment {
    std::string text;     // raw text of this segment
    bool is_command;      // true for the first segment (shell command)
};

/// Split a command line by `|>` (outside of quotes).
/// The first segment is the shell command; subsequent segments are operators.
std::vector<PipelineSegment> split_pipeline(const std::string &line);

/// Returns true if the line contains `|>` outside of quotes.
bool has_structured_pipe(const std::string &line);

// ── Input auto-detection ──────────────────────────────────────

/// Parse text into a JSON array:
///  - valid JSON array  -> return as-is
///  - valid JSON object -> wrap in single-element array
///  - otherwise         -> split lines, create [{"line": "...", "index": N}, ...]
JsonValue parse_input(const std::string &text);

// ── Operator parsing ──────────────────────────────────────────

struct ParsedOperator {
    std::string name;
    std::vector<std::string> args;
};

/// Parse an operator segment string like "where size > 100" into
/// name="where", args=["size", ">", "100"].
ParsedOperator parse_operator(const std::string &segment);

// ── Operator application ──────────────────────────────────────

/// Apply a single named operator with arguments to the input data.
/// Returns the transformed data, or a single-element array with an
/// "error" field if the operator is unknown.
JsonValue apply_operator(const std::string &op_name,
                         const std::vector<std::string> &args,
                         const JsonValue &input);

// ── Individual operators ──────────────────────────────────────

JsonValue op_where(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_sort_by(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_select(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_reject(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_first(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_last(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_count(const std::vector<std::string> &args, const JsonValue &input);
JsonValue op_uniq(const std::vector<std::string> &args, const JsonValue &input);

// ── Output formatters ─────────────────────────────────────────

std::string to_json(const JsonValue &data);
std::string to_csv(const JsonValue &data);
std::string render_table(const JsonValue &data);

// ── Full pipeline execution ───────────────────────────────────

/// Execute a full structured pipeline.  The first segment's command is
/// run as a shell command (stdout captured), then each subsequent
/// operator is applied in order.  Returns the final rendered output.
std::string execute_pipeline(const std::string &command_line);

} // namespace structured_pipe
} // namespace tash

#endif // TASH_AI_ENABLED
#endif // TASH_CORE_STRUCTURED_PIPE_H
