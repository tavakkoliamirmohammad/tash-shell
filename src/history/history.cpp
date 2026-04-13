#include "tash/history.h"

using namespace std;
using namespace replxx;

string history_file_path() {
    const char *home = getenv("HOME");
    if (!home) return "";
    return string(home) + "/.tash_history";
}

bool should_record_history(const string &line, Replxx &rx) {
    if (line.empty()) return false;

    // Ignore lines starting with space (for sensitive commands)
    if (line[0] == ' ') return false;

    // Ignore duplicates: check last history entry via scan
    Replxx::HistoryScan hs(rx.history_scan());
    if (hs.next()) {
        Replxx::HistoryEntry he(hs.get());
        if (string(he.text()) == line) return false;
    }

    return true;
}
