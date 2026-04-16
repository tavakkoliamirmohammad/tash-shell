#ifndef TASH_SQLITE_HISTORY_PROVIDER_H
#define TASH_SQLITE_HISTORY_PROVIDER_H

#include "tash/plugin.h"
#include <sqlite3.h>
#include <string>
#include <vector>

class SqliteHistoryProvider : public IHistoryProvider {
public:
    // Opens or creates the database at the given path.
    // If db_path is empty, defaults to ~/.tash/history.db.
    explicit SqliteHistoryProvider(const std::string &db_path = "");
    ~SqliteHistoryProvider() override;

    // Non-copyable
    SqliteHistoryProvider(const SqliteHistoryProvider &) = delete;
    SqliteHistoryProvider &operator=(const SqliteHistoryProvider &) = delete;

    std::string name() const override;
    void record(const HistoryEntry &entry) override;
    std::vector<HistoryEntry> search(
        const std::string &query,
        const SearchFilter &filter) const override;
    std::vector<HistoryEntry> recent(int count) const override;

private:
    void init_schema();
    void migrate_plain_text_history();
    HistoryEntry row_to_entry(sqlite3_stmt *stmt) const;

    sqlite3 *db_;
    std::string db_path_;
    std::string last_recorded_command_;
    std::string last_recorded_session_;
};

#endif // TASH_SQLITE_HISTORY_PROVIDER_H
