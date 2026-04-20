
#include "tash/core/structured_pipe.h"
#include "tash/core/executor.h"
#include "tash/core/parser.h"
#include "tash/shell.h"
#include "tash/util/quote_state.h"

#include <algorithm>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <stdexcept>

namespace tash::structured_pipe {

// ═══════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════

static std::string trim_str(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

/// Truncate a string to max_len characters, appending "..." if truncated.
static std::string truncate(const std::string &s, size_t max_len) {
    if (s.size() <= max_len) return s;
    if (max_len <= 3) return s.substr(0, max_len);
    return s.substr(0, max_len - 3) + "...";
}

/// Strip surrounding quotes from a token (single or double).
static std::string strip_quotes(const std::string &s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') ||
            (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

/// Try to parse a string as a number (double).  Returns true on success.
static bool try_parse_number(const std::string &s, double &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    double val = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = val;
    return true;
}

/// Auto-type a value: if it parses as a number, return json number;
/// otherwise return json string (stripping quotes).
static JsonValue auto_type_value(const std::string &raw) {
    std::string val = strip_quotes(raw);
    double num;
    if (try_parse_number(val, num)) {
        // Preserve integer representation when possible
        if (num == std::floor(num) && std::abs(num) < 1e15) {
            return JsonValue(static_cast<int64_t>(num));
        }
        return JsonValue(num);
    }
    return JsonValue(val);
}

/// JSON value to display string for table/csv rendering.
static std::string value_to_string(const JsonValue &v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_float()) {
        std::ostringstream oss;
        oss << v.get<double>();
        return oss.str();
    }
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_null()) return "";
    return v.dump();
}

/// Compare two JSON values.  Returns <0, 0, >0.
static int compare_json(const JsonValue &a, const JsonValue &b) {
    // Both numbers
    if (a.is_number() && b.is_number()) {
        double da = a.get<double>();
        double db = b.get<double>();
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
    // Both strings
    if (a.is_string() && b.is_string()) {
        return a.get<std::string>().compare(b.get<std::string>());
    }
    // Fallback: compare string representations
    std::string sa = value_to_string(a);
    std::string sb = value_to_string(b);
    return sa.compare(sb);
}

/// Tokenize a string respecting quotes, splitting on whitespace.
static std::vector<std::string> tokenize_args(const std::string &input) {
    std::vector<std::string> tokens;
    std::string current;
    tash::util::QuoteState qs;
    size_t i = 0;
    size_t len = input.size();

    while (i < len) {
        char c = input[i];
        if (qs.consume(c)) {
            current += c;
            ++i;
        } else if (!qs.any_active() && (c == ' ' || c == '\t')) {
            std::string t = trim_str(current);
            if (!t.empty()) tokens.push_back(t);
            current.clear();
            ++i;
        } else {
            current += c;
            ++i;
        }
    }
    std::string t = trim_str(current);
    if (!t.empty()) tokens.push_back(t);
    return tokens;
}

// ═══════════════════════════════════════════════════════════════
// Pipeline parsing
// ═══════════════════════════════════════════════════════════════

bool has_structured_pipe(const std::string &line) {
    tash::util::QuoteState qs;
    size_t len = line.size();
    for (size_t i = 0; i < len; ++i) {
        char c = line[i];
        if (qs.consume(c)) continue;
        if (!qs.any_active() && c == '|' && i + 1 < len && line[i + 1] == '>') {
            return true;
        }
    }
    return false;
}

std::vector<PipelineSegment> split_pipeline(const std::string &line) {
    std::vector<PipelineSegment> segments;
    std::string current;
    tash::util::QuoteState qs;
    size_t len = line.size();

    for (size_t i = 0; i < len; ++i) {
        char c = line[i];
        if (qs.consume(c)) {
            current += c;
        } else if (!qs.any_active() && c == '|' && i + 1 < len && line[i + 1] == '>') {
            std::string seg = trim_str(current);
            if (!seg.empty()) {
                PipelineSegment ps;
                ps.text = seg;
                ps.is_command = segments.empty();
                segments.push_back(ps);
            }
            current.clear();
            i += 1; // skip '>', loop will advance past '>'
        } else {
            current += c;
        }
    }

    std::string seg = trim_str(current);
    if (!seg.empty()) {
        PipelineSegment ps;
        ps.text = seg;
        ps.is_command = segments.empty();
        segments.push_back(ps);
    }

    return segments;
}

// ═══════════════════════════════════════════════════════════════
// Input auto-detection
// ═══════════════════════════════════════════════════════════════

JsonValue parse_input(const std::string &text) {
    std::string trimmed = trim_str(text);
    if (trimmed.empty()) {
        return JsonValue::array();
    }

    // Try parsing as JSON
    try {
        JsonValue parsed = JsonValue::parse(trimmed);
        if (parsed.is_array()) {
            return parsed;
        }
        if (parsed.is_object()) {
            JsonValue arr = JsonValue::array();
            arr.push_back(parsed);
            return arr;
        }
        // If it's a scalar JSON value, fall through to line-based
    } catch (...) {
        // Not valid JSON, fall through
    }

    // Line-based parsing
    JsonValue arr = JsonValue::array();
    std::istringstream iss(text);
    std::string line;
    int index = 0;
    while (std::getline(iss, line)) {
        // Skip completely empty lines at the end
        if (line.empty() && iss.eof()) break;
        JsonValue obj;
        obj["line"] = line;
        obj["index"] = index;
        arr.push_back(obj);
        ++index;
    }
    return arr;
}

// ═══════════════════════════════════════════════════════════════
// Operator parsing
// ═══════════════════════════════════════════════════════════════

ParsedOperator parse_operator(const std::string &segment) {
    ParsedOperator op;
    std::vector<std::string> tokens = tokenize_args(segment);
    if (tokens.empty()) {
        return op;
    }
    op.name = tokens[0];
    for (size_t i = 1; i < tokens.size(); ++i) {
        op.args.push_back(tokens[i]);
    }
    return op;
}

// ═══════════════════════════════════════════════════════════════
// Operator implementations
// ═══════════════════════════════════════════════════════════════

JsonValue op_where(const std::vector<std::string> &args, const JsonValue &input) {
    if (args.size() < 3) return input;
    if (!input.is_array()) return input;

    std::string field = args[0];
    std::string op = args[1];
    std::string raw_value = args[2];
    // Join remaining args for multi-word quoted values
    for (size_t i = 3; i < args.size(); ++i) {
        raw_value += " " + args[i];
    }
    JsonValue compare_val = auto_type_value(raw_value);

    JsonValue result = JsonValue::array();
    for (const auto &item : input) {
        if (!item.is_object()) continue;
        if (item.find(field) == item.end()) continue;

        const JsonValue &fval = item[field];
        int cmp = compare_json(fval, compare_val);
        bool match = false;

        if (op == "==" || op == "=") {
            match = (cmp == 0);
        } else if (op == "!=") {
            match = (cmp != 0);
        } else if (op == ">") {
            match = (cmp > 0);
        } else if (op == "<") {
            match = (cmp < 0);
        } else if (op == ">=") {
            match = (cmp >= 0);
        } else if (op == "<=") {
            match = (cmp <= 0);
        }

        if (match) {
            result.push_back(item);
        }
    }
    return result;
}

JsonValue op_sort_by(const std::vector<std::string> &args, const JsonValue &input) {
    if (args.empty()) return input;
    if (!input.is_array()) return input;

    std::string field = args[0];
    bool descending = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--desc" || args[i] == "-d") {
            descending = true;
        }
    }

    // Copy to a vector for sorting
    std::vector<JsonValue> items;
    for (const auto &item : input) {
        items.push_back(item);
    }

    std::stable_sort(items.begin(), items.end(),
        [&field, descending](const JsonValue &a, const JsonValue &b) -> bool {
            // Items missing the field go to the end
            bool a_has = a.is_object() && a.find(field) != a.end();
            bool b_has = b.is_object() && b.find(field) != b.end();
            if (!a_has && !b_has) return false;
            if (!a_has) return false;
            if (!b_has) return true;

            int cmp = compare_json(a[field], b[field]);
            return descending ? (cmp > 0) : (cmp < 0);
        });

    JsonValue result = JsonValue::array();
    for (const auto &item : items) {
        result.push_back(item);
    }
    return result;
}

JsonValue op_select(const std::vector<std::string> &args, const JsonValue &input) {
    if (args.empty()) return input;
    if (!input.is_array()) return input;

    JsonValue result = JsonValue::array();
    for (const auto &item : input) {
        if (!item.is_object()) continue;
        JsonValue obj;
        for (const auto &field : args) {
            if (item.find(field) != item.end()) {
                obj[field] = item[field];
            }
        }
        result.push_back(obj);
    }
    return result;
}

JsonValue op_reject(const std::vector<std::string> &args, const JsonValue &input) {
    if (args.empty()) return input;
    if (!input.is_array()) return input;

    // Build a set of fields to reject
    std::vector<std::string> reject_fields(args.begin(), args.end());

    JsonValue result = JsonValue::array();
    for (const auto &item : input) {
        if (!item.is_object()) {
            result.push_back(item);
            continue;
        }
        JsonValue obj;
        for (auto it = item.begin(); it != item.end(); ++it) {
            bool rejected = false;
            for (const auto &rf : reject_fields) {
                if (it.key() == rf) {
                    rejected = true;
                    break;
                }
            }
            if (!rejected) {
                obj[it.key()] = it.value();
            }
        }
        result.push_back(obj);
    }
    return result;
}

JsonValue op_first(const std::vector<std::string> &args, const JsonValue &input) {
    if (!input.is_array()) return input;
    size_t n = 1;
    if (!args.empty()) {
        double parsed;
        if (try_parse_number(args[0], parsed) && parsed > 0) {
            n = static_cast<size_t>(parsed);
        }
    }
    size_t count = std::min(n, input.size());
    JsonValue result = JsonValue::array();
    for (size_t i = 0; i < count; ++i) {
        result.push_back(input[i]);
    }
    return result;
}

JsonValue op_last(const std::vector<std::string> &args, const JsonValue &input) {
    if (!input.is_array()) return input;
    size_t n = 1;
    if (!args.empty()) {
        double parsed;
        if (try_parse_number(args[0], parsed) && parsed > 0) {
            n = static_cast<size_t>(parsed);
        }
    }
    size_t count = std::min(n, input.size());
    size_t start = input.size() - count;
    JsonValue result = JsonValue::array();
    for (size_t i = start; i < input.size(); ++i) {
        result.push_back(input[i]);
    }
    return result;
}

JsonValue op_count(const std::vector<std::string> & /*args*/, const JsonValue &input) {
    JsonValue result = JsonValue::array();
    JsonValue obj;
    if (input.is_array()) {
        obj["count"] = static_cast<int64_t>(input.size());
    } else {
        obj["count"] = 0;
    }
    result.push_back(obj);
    return result;
}

JsonValue op_uniq(const std::vector<std::string> & /*args*/, const JsonValue &input) {
    if (!input.is_array() || input.empty()) return input;

    JsonValue result = JsonValue::array();
    result.push_back(input[0]);
    for (size_t i = 1; i < input.size(); ++i) {
        if (input[i] != input[i - 1]) {
            result.push_back(input[i]);
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// Operator dispatch
// ═══════════════════════════════════════════════════════════════

JsonValue apply_operator(const std::string &op_name,
                         const std::vector<std::string> &args,
                         const JsonValue &input) {
    if (op_name == "where")     return op_where(args, input);
    if (op_name == "sort-by")   return op_sort_by(args, input);
    if (op_name == "select")    return op_select(args, input);
    if (op_name == "reject")    return op_reject(args, input);
    if (op_name == "first")     return op_first(args, input);
    if (op_name == "last")      return op_last(args, input);
    if (op_name == "count")     return op_count(args, input);
    if (op_name == "uniq")      return op_uniq(args, input);

    // Output operators return data unchanged (formatting is done later)
    if (op_name == "to-json" || op_name == "to-csv" || op_name == "to-table") {
        return input;
    }

    // Unknown operator
    JsonValue err = JsonValue::array();
    JsonValue obj;
    obj["error"] = "unknown operator: " + op_name;
    err.push_back(obj);
    return err;
}

// ═══════════════════════════════════════════════════════════════
// Output formatters
// ═══════════════════════════════════════════════════════════════

std::string to_json(const JsonValue &data) {
    return data.dump(2);
}

std::string to_csv(const JsonValue &data) {
    if (!data.is_array() || data.empty()) return "";

    // Collect all column names from all rows (ordered by first appearance)
    std::vector<std::string> columns;
    for (const auto &row : data) {
        if (!row.is_object()) continue;
        for (auto it = row.begin(); it != row.end(); ++it) {
            bool found = false;
            for (const auto &c : columns) {
                if (c == it.key()) { found = true; break; }
            }
            if (!found) {
                columns.push_back(it.key());
            }
        }
    }

    if (columns.empty()) return "";

    std::ostringstream oss;

    // Header
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) oss << ",";
        oss << columns[i];
    }
    oss << "\n";

    // Rows
    for (const auto &row : data) {
        if (!row.is_object()) continue;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) oss << ",";
            if (row.find(columns[i]) != row.end()) {
                std::string val = value_to_string(row[columns[i]]);
                // Escape CSV: if value contains comma, quote, or newline, quote it
                bool needs_quoting = false;
                for (char c : val) {
                    if (c == ',' || c == '"' || c == '\n') {
                        needs_quoting = true;
                        break;
                    }
                }
                if (needs_quoting) {
                    oss << '"';
                    for (char c : val) {
                        if (c == '"') oss << '"';
                        oss << c;
                    }
                    oss << '"';
                } else {
                    oss << val;
                }
            }
        }
        oss << "\n";
    }

    return oss.str();
}

std::string render_table(const JsonValue &data) {
    if (!data.is_array() || data.empty()) return "";

    // Collect columns (order by first appearance across all rows)
    std::vector<std::string> columns;
    for (const auto &row : data) {
        if (!row.is_object()) continue;
        for (auto it = row.begin(); it != row.end(); ++it) {
            bool found = false;
            for (const auto &c : columns) {
                if (c == it.key()) { found = true; break; }
            }
            if (!found) {
                columns.push_back(it.key());
            }
        }
    }

    if (columns.empty()) return "";

    static const size_t MAX_COL_WIDTH = 40;

    // Calculate column widths
    std::vector<size_t> widths(columns.size());
    for (size_t i = 0; i < columns.size(); ++i) {
        widths[i] = columns[i].size();
    }
    for (const auto &row : data) {
        if (!row.is_object()) continue;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (row.find(columns[i]) != row.end()) {
                std::string val = truncate(value_to_string(row[columns[i]]), MAX_COL_WIDTH);
                if (val.size() > widths[i]) widths[i] = val.size();
            }
        }
    }

    // Cap widths
    for (size_t i = 0; i < widths.size(); ++i) {
        if (widths[i] > MAX_COL_WIDTH) widths[i] = MAX_COL_WIDTH;
    }

    // Build horizontal lines
    // top:    ┌──┬──┐
    // mid:    ├──┼──┤
    // bottom: └──┴──┘
    auto build_line = [&](const std::string &left, const std::string &mid,
                          const std::string &cross, const std::string &right) -> std::string {
        std::string line = left;
        for (size_t i = 0; i < columns.size(); ++i) {
            for (size_t j = 0; j < widths[i] + 2; ++j) line += mid;
            if (i + 1 < columns.size()) line += cross;
        }
        line += right;
        return line;
    };

    // Unicode box-drawing chars (UTF-8)
    std::string top_line    = build_line("\xe2\x94\x8c", "\xe2\x94\x80", "\xe2\x94\xac", "\xe2\x94\x90");
    std::string mid_line    = build_line("\xe2\x94\x9c", "\xe2\x94\x80", "\xe2\x94\xbc", "\xe2\x94\xa4");
    std::string bottom_line = build_line("\xe2\x94\x94", "\xe2\x94\x80", "\xe2\x94\xb4", "\xe2\x94\x98");
    std::string vert        = "\xe2\x94\x82"; // │

    auto pad_cell = [](const std::string &text, size_t width) -> std::string {
        std::string result = text;
        while (result.size() < width) result += ' ';
        return result;
    };

    std::ostringstream oss;

    // Top border
    oss << top_line << "\n";

    // Header row
    oss << vert;
    for (size_t i = 0; i < columns.size(); ++i) {
        oss << " " << pad_cell(truncate(columns[i], MAX_COL_WIDTH), widths[i]) << " " << vert;
    }
    oss << "\n";

    // Header separator
    oss << mid_line << "\n";

    // Data rows
    for (const auto &row : data) {
        if (!row.is_object()) continue;
        oss << vert;
        for (size_t i = 0; i < columns.size(); ++i) {
            std::string val;
            if (row.find(columns[i]) != row.end()) {
                val = truncate(value_to_string(row[columns[i]]), MAX_COL_WIDTH);
            }
            oss << " " << pad_cell(val, widths[i]) << " " << vert;
        }
        oss << "\n";
    }

    // Bottom border
    oss << bottom_line << "\n";

    return oss.str();
}

// ═══════════════════════════════════════════════════════════════
// Full pipeline execution
// ═══════════════════════════════════════════════════════════════

std::string execute_pipeline(const std::string &command_line, ::ShellState &state) {
    std::vector<PipelineSegment> segments = split_pipeline(command_line);
    if (segments.empty()) return "";

    // Route the first segment through the safety-hook system so that
    // before_command / after_command providers observe (and may veto)
    // the inner shell command — same fix as Task 8 applied to $(...).
    HookedCaptureResult hooked =
        run_command_with_hooks_capture(segments[0].text, state);
    if (hooked.skipped) {
        // Safety hook vetoed the command — return empty output immediately.
        // Downstream operators are skipped to avoid rendering formatters
        // (e.g., to-json, render_table) on nothing.
        return "";
    }
    std::string output = std::move(hooked.captured_stdout);

    // Parse output into structured data
    JsonValue data = parse_input(output);

    // Track the last output format operator seen
    std::string output_format;

    // Apply subsequent operators
    for (size_t i = 1; i < segments.size(); ++i) {
        ParsedOperator op = parse_operator(segments[i].text);
        if (op.name.empty()) continue;

        // Track output format
        if (op.name == "to-json" || op.name == "to-csv" || op.name == "to-table") {
            output_format = op.name;
        }

        data = apply_operator(op.name, op.args, data);
    }

    // Format output
    if (output_format == "to-json") return to_json(data) + "\n";
    if (output_format == "to-csv")  return to_csv(data);

    // Default: to-table
    std::string table = render_table(data);
    if (table.empty() && data.is_array() && data.empty()) return "";
    return table;
}

} // namespace tash::structured_pipe

