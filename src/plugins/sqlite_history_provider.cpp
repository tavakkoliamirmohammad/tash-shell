#include "tash/plugins/sqlite_history_provider.h"
#include "tash/util/config_resolver.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

// ── Helpers ───────────────────────────────────────────────────

static std::string default_db_path() {
    return tash::config::get_history_db_path();
}

static std::string plain_text_history_path() {
    return tash::config::get_history_file_path();
}

static bool file_exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static void ensure_directory(const std::string &path) {
    if (path.empty()) return;
    mkdir(path.c_str(), 0755);
}

// ── Constructor / Destructor ──────────────────────────────────

SqliteHistoryProvider::SqliteHistoryProvider(const std::string &db_path)
    : db_(nullptr)
    , db_path_(db_path.empty() ? default_db_path() : db_path) {

    if (db_path_.empty()) {
        return;
    }

    // Ensure parent directory exists
    std::string::size_type pos = db_path_.rfind('/');
    if (pos != std::string::npos) {
        ensure_directory(db_path_.substr(0, pos));
    }

    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "tash: failed to open history database: "
                  << sqlite3_errmsg(db_) << std::endl;
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return;
    }

    // Enable WAL mode for better concurrent access
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    init_schema();
    migrate_plain_text_history();
}

SqliteHistoryProvider::~SqliteHistoryProvider() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// ── IHistoryProvider interface ────────────────────────────────

std::string SqliteHistoryProvider::name() const {
    return "sqlite-history";
}

void SqliteHistoryProvider::record(const HistoryEntry &entry) {
    if (!db_) return;

    // Privacy: skip commands starting with a space
    if (!entry.command.empty() && entry.command[0] == ' ') {
        return;
    }

    // Dedup: skip if last command in this session is identical
    if (!entry.session_id.empty() &&
        entry.session_id == last_recorded_session_ &&
        entry.command == last_recorded_command_) {
        return;
    }

    const char *sql =
        "INSERT INTO history "
        "(command, timestamp, directory, exit_code, duration_ms, "
        " hostname, session_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, entry.command.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, entry.timestamp);

    if (entry.directory.empty()) {
        sqlite3_bind_null(stmt, 3);
    } else {
        sqlite3_bind_text(stmt, 3, entry.directory.c_str(), -1, SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(stmt, 4, entry.exit_code);
    sqlite3_bind_int(stmt, 5, entry.duration_ms);

    if (entry.hostname.empty()) {
        sqlite3_bind_null(stmt, 6);
    } else {
        sqlite3_bind_text(stmt, 6, entry.hostname.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (entry.session_id.empty()) {
        sqlite3_bind_null(stmt, 7);
    } else {
        sqlite3_bind_text(stmt, 7, entry.session_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        last_recorded_command_ = entry.command;
        last_recorded_session_ = entry.session_id;
    }
}

std::vector<HistoryEntry> SqliteHistoryProvider::search(
    const std::string &query,
    const SearchFilter &filter) const {

    std::vector<HistoryEntry> results;
    if (!db_) return results;

    // Build dynamic SQL with optional filters
    std::ostringstream sql;
    sql << "SELECT id, command, timestamp, directory, exit_code, "
           "duration_ms, hostname, session_id "
           "FROM history WHERE command LIKE ?";

    if (!filter.directory.empty()) {
        sql << " AND directory = ?";
    }
    if (filter.exit_code != -1) {
        sql << " AND exit_code = ?";
    }
    if (filter.since != 0) {
        sql << " AND timestamp >= ?";
    }

    sql << " ORDER BY timestamp DESC";

    int limit = filter.limit > 0 ? filter.limit : 50;
    sql << " LIMIT ?";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return results;
    }

    // Bind parameters
    int idx = 1;
    std::string like_pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, idx++, like_pattern.c_str(), -1, SQLITE_TRANSIENT);

    if (!filter.directory.empty()) {
        sqlite3_bind_text(stmt, idx++, filter.directory.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (filter.exit_code != -1) {
        sqlite3_bind_int(stmt, idx++, filter.exit_code);
    }
    if (filter.since != 0) {
        sqlite3_bind_int64(stmt, idx++, filter.since);
    }

    sqlite3_bind_int(stmt, idx, limit);

    // Collect results
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(row_to_entry(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<HistoryEntry> SqliteHistoryProvider::recent(int count) const {
    std::vector<HistoryEntry> results;
    if (!db_) return results;

    const char *sql =
        "SELECT id, command, timestamp, directory, exit_code, "
        "duration_ms, hostname, session_id "
        "FROM history ORDER BY timestamp DESC LIMIT ?;";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int(stmt, 1, count > 0 ? count : 50);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(row_to_entry(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

// ── Private helpers ───────────────────────────────────────────

void SqliteHistoryProvider::init_schema() {
    if (!db_) return;

    const char *schema_sql =
        "CREATE TABLE IF NOT EXISTS history ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    command TEXT NOT NULL,"
        "    timestamp INTEGER NOT NULL,"
        "    directory TEXT,"
        "    exit_code INTEGER,"
        "    duration_ms INTEGER,"
        "    hostname TEXT,"
        "    session_id TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_history_command "
        "    ON history(command);"
        "CREATE INDEX IF NOT EXISTS idx_history_directory "
        "    ON history(directory);"
        "CREATE INDEX IF NOT EXISTS idx_history_timestamp "
        "    ON history(timestamp DESC);";

    char *err = nullptr;
    int rc = sqlite3_exec(db_, schema_sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "tash: failed to create history schema: "
                  << (err ? err : "unknown error") << std::endl;
        sqlite3_free(err);
    }
}

void SqliteHistoryProvider::migrate_plain_text_history() {
    if (!db_) return;

    // Only migrate if using the default path
    std::string default_path = default_db_path();
    if (db_path_ != default_path) return;

    std::string txt_path = plain_text_history_path();
    if (txt_path.empty() || !file_exists(txt_path)) return;

    std::ifstream infile(txt_path);
    if (!infile.is_open()) return;

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    // Use a transaction for bulk insert
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char *sql =
        "INSERT INTO history (command, timestamp) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, line.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, now);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    infile.close();

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    // Rename the old file to .bak
    std::string bak_path = txt_path + ".bak";
    std::rename(txt_path.c_str(), bak_path.c_str());
}

HistoryEntry SqliteHistoryProvider::row_to_entry(sqlite3_stmt *stmt) const {
    HistoryEntry entry;

    entry.id = sqlite3_column_int64(stmt, 0);

    const char *cmd = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 1));
    entry.command = cmd ? cmd : "";

    entry.timestamp = sqlite3_column_int64(stmt, 2);

    const char *dir = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 3));
    entry.directory = dir ? dir : "";

    entry.exit_code = sqlite3_column_int(stmt, 4);
    entry.duration_ms = sqlite3_column_int(stmt, 5);

    const char *host = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 6));
    entry.hostname = host ? host : "";

    const char *sess = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 7));
    entry.session_id = sess ? sess : "";

    return entry;
}
