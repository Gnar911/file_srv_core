#include "sqlite/log_index_db.h"

#include <cstdint>
#include <string>
#include <vector>

#include "can_analyzer_log.h"

#include "sqlite/sqlite_wrapper_RAII.h"

namespace {

constexpr int SQLITE_ROW = 100;

std::pair<int32_t, int32_t> to_page_window(int32_t first, int32_t last) {
    if (last < first) {
        return {first, 0};
    }
    return {first, (last - first) + 1};
}

// Normalizes channel text: trim whitespace and lowercase.
static std::string normalize_channel_key(const std::string& channel) {
    if (channel.empty()) {
        return "unknown";
    }
    std::string key = channel;
    while (!key.empty() && static_cast<unsigned char>(key.back()) <= ' ') {
        key.pop_back();
    }
    size_t start = 0;
    while (start < key.size() && static_cast<unsigned char>(key[start]) <= ' ') {
        ++start;
    }
    if (start > 0) {
        key.erase(0, start);
    }
    if (key.empty()) {
        return "unknown";
    }
    for (char& c : key) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return key;
}

} // namespace


LogIndexDatabase::LogIndexDatabase(std::string db_path)
        : db_path_(std::move(db_path)),
            db_(db_path_) {

        static const char* kInsertSql =
                "INSERT OR REPLACE INTO log_index "
                "(row_index, timestamp, can_id, direction, channel, changed) "
                "VALUES (?, ?, ?, ?, ?, ?);";

    static const char* kSchemaSql =
        "CREATE TABLE IF NOT EXISTS log_index ("
        "  row_index INTEGER PRIMARY KEY,"
        "  timestamp REAL    NOT NULL,"
        "  can_id    INTEGER NOT NULL,"
        "  direction INTEGER NOT NULL,"
        "  channel   TEXT    NOT NULL,"
        "  changed   INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_log_canid   ON log_index(can_id);"
        "CREATE INDEX IF NOT EXISTS idx_log_channel ON log_index(channel);"
        "CREATE INDEX IF NOT EXISTS idx_log_dir     ON log_index(direction);"
        "CREATE INDEX IF NOT EXISTS idx_log_changed ON log_index(changed) WHERE changed = 1;";

    sqlitew::exec(db_.get(), kSchemaSql, nullptr, nullptr, nullptr);
    stmt_.prepare(db_, kInsertSql);
}

std::string LogIndexDatabase::db_path() const {
    return db_path_;
}

int32_t LogIndexDatabase::begin_transaction() {
    sqlitew::exec(db_.get(), "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    return 0;
}

int32_t LogIndexDatabase::commit_transaction() {
    sqlitew::exec(db_.get(), "COMMIT;", nullptr, nullptr, nullptr);
    return 0;
}

/// NOTE:
// Adding context data like row_index or changed
// THis append_entry is like next, it write in database in hot path call, not using as the normal application call
// If there is catch out -> it will crashed the app, this should be called inside process
void LogIndexDatabase::append_index(
                                    uint32_t row_index,
                                    const ParsedEntry& entry)
{
    const std::string channel = normalize_channel_key(entry.channel);

    stmt_.bind_int64(1, row_index);
    stmt_.bind_double(2, entry.timestamp);
    stmt_.bind_int64(3, entry.can_id);
    stmt_.bind_int64(4, entry.direction);
    stmt_.bind_text(5, channel.c_str(), -1, nullptr);
    stmt_.bind_int64(6, static_cast<sqlite3_int64>(entry.changed ? 1 : 0));

    stmt_.step();

    stmt_.reset_and_clear();
}

/// @brief THis is the remark function of the previous one. No need to do query the previous for calculate metadata
///        since the ParsedEntry store the pointer to the neighbor entries.
/// @param row_index 
/// @param entry 
/// @return 
bool LogIndexDatabase::update_index(uint32_t row_index,
                                    const ParsedEntry& entry)
{
    static const char* kUpdateSql =
        "UPDATE log_index "
        "SET timestamp = ?, "
        "    can_id = ?, "
        "    direction = ?, "
        "    channel = ?, "
        "    changed = ? "
        "WHERE row_index = ?;";

    Statement update_stmt(db_, kUpdateSql);
    const std::string channel = normalize_channel_key(entry.channel);

    update_stmt.bind_double(1, entry.timestamp);
    update_stmt.bind_int64(2, static_cast<sqlite3_int64>(entry.can_id));
    update_stmt.bind_int64(3, static_cast<sqlite3_int64>(entry.direction));
    update_stmt.bind_text(4, channel.c_str(), -1, nullptr);
    update_stmt.bind_int64(5, static_cast<sqlite3_int64>(entry.changed));
    update_stmt.bind_int64(6, static_cast<sqlite3_int64>(row_index));

    update_stmt.step();
    return true;
}



std::vector<uint32_t> LogIndexDatabase::query_row_indices(const LogQuery& query,
                                                          int32_t first,
                                                          int32_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    if (page_size <= 0) {
        return {};
    }

    // Build the WHERE clause from fixed column names + the *count* of "?"
    // placeholders. Values are bound below (injection-safe).
    std::string sql = "SELECT row_index FROM log_index WHERE 1=1";

    auto append_in = [&sql](const char* column, size_t count) {
        if (count == 0) {
            return;
        }
        sql += " AND ";
        sql += column;
        sql += " IN (";
        for (size_t i = 0; i < count; ++i) {
            sql += (i == 0) ? "?" : ",?";
        }
        sql += ")";
    };

    append_in("can_id", query.can_ids.size());
    append_in("direction", query.directions.size());
    append_in("channel", query.channels.size());
    if (query.changed_only) {
        sql += " AND changed = 1";
    }
    if (query.has_time_range) {
        sql += " AND timestamp BETWEEN ? AND ?";
    }
    sql += " ORDER BY row_index LIMIT ? OFFSET ?;";

    Statement query_stmt(db_, sql.c_str());

    int bind_idx = 1;
    for (const uint32_t can_id : query.can_ids) {
        query_stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(can_id));
    }
    for (const uint8_t direction : query.directions) {
        query_stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(direction));
    }
    for (const std::string& channel : query.channels) {
        query_stmt.bind_text(bind_idx++, channel.c_str(), -1, nullptr);
    }
    if (query.has_time_range) {
        query_stmt.bind_double(bind_idx++, query.first_ts);
        query_stmt.bind_double(bind_idx++, query.last_ts);
    }
    query_stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(page_size));   // LIMIT
    query_stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(first_line));  // OFFSET

    std::vector<uint32_t> rows;
    rows.reserve(static_cast<size_t>(page_size));
    while (query_stmt.step() == SQLITE_ROW) {
        rows.push_back(static_cast<uint32_t>(query_stmt.column_int64(0)));
    }

    return rows;
}


bool LogIndexDatabase::get_first_last_timestamp(double& out_first_ts,
                                                             double& out_last_ts) const {
    out_first_ts = 0.0;
    out_last_ts = 0.0;

    static const char* kSql = "SELECT COUNT(1), MIN(timestamp), MAX(timestamp) FROM log_index;";

    Statement stmt(db_, kSql);
    stmt.step();

    const sqlite3_int64 count = stmt.column_int64(0);
    if (count == 0) {
        return false;
    }

    out_first_ts = stmt.column_double(1);
    out_last_ts = stmt.column_double(2);
    return true;
}

uint32_t LogIndexDatabase::row_count() const {
    static const char* kSql = "SELECT COUNT(1) FROM log_index;";
    Statement stmt(db_, kSql);
    stmt.step();
    const sqlite3_int64 count = stmt.column_int64(0);

    return static_cast<uint32_t>(count);
}
