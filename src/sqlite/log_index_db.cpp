#include "sqlite/log_index_db.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "can_analyzer_log.h"

#include "sqlite/sqlite_wrapper_RAII.h"
#include <stdexcept>
#include <exception>

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

LogIndexDatabase::~LogIndexDatabase() {
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

// bool LogIndexDatabase::update_index(uint32_t row_index, const ParsedEntry& entry) {
//     bool opened_here = false;
//     if (db_ == nullptr) {
//         sqlitew::open(db_path_.c_str(), &db_);
//         opened_here = true;
//     }
//     static const char* kSelectCanIdSql =
//         "SELECT can_id FROM log_index WHERE row_index = ? LIMIT 1;";
//     static const char* kFindPrevSql =
//         "SELECT row_index FROM log_index "
//         "WHERE can_id = ? AND row_index < ? "
//         "ORDER BY row_index DESC LIMIT 1;";
//     static const char* kFindNextSql =
//         "SELECT row_index FROM log_index "
//         "WHERE can_id = ? AND row_index > ? "
//         "ORDER BY row_index ASC LIMIT 1;";
//     static const char* kUpdateChangedSql =
//         "UPDATE log_index SET changed = ? WHERE row_index = ?;";
//     static const char* kUpdateRowSql =
//         "UPDATE log_index "
//         "SET timestamp = ?, can_id = ?, direction = ?, channel = ?, changed = ? "
//         "WHERE row_index = ?;";

//     sqlite3_stmt* stmt_select_can_id = nullptr;
//     sqlite3_stmt* stmt_find_prev_row = nullptr;
//     sqlite3_stmt* stmt_find_next_row = nullptr;
//     sqlite3_stmt* stmt_update_changed = nullptr;
//     sqlite3_stmt* stmt_update_row = nullptr;

//     auto payload_changed = [](const LogRecord& prev, const LogRecord& cur) -> bool {
//         const uint8_t prev_len = (prev.data_len <= sizeof(prev.data))
//             ? prev.data_len
//             : static_cast<uint8_t>(sizeof(prev.data));
//         const uint8_t cur_len = (cur.data_len <= sizeof(cur.data))
//             ? cur.data_len
//             : static_cast<uint8_t>(sizeof(cur.data));
//         if (prev_len != cur_len) {
//             return true;
//         }
//         return cur_len > 0 && std::memcmp(prev.data, cur.data, cur_len) != 0;
//     };

//     auto get_can_id = [&stmt_select_can_id](uint32_t idx) -> uint32_t {
//         sqlitew::reset(stmt_select_can_id);
//         sqlitew::clear_bindings(stmt_select_can_id);
//         sqlitew::bind_int64(stmt_select_can_id, 1, static_cast<sqlite3_int64>(idx));
//         if (sqlitew::step(stmt_select_can_id) != SQLITE_ROW) {
//             sqlitew::reset(stmt_select_can_id);
//             sqlitew::clear_bindings(stmt_select_can_id);
//             throw std::runtime_error("update_entry target row not found in log_index");
//         }
//         const uint32_t can_id = static_cast<uint32_t>(sqlitew::column_int64(stmt_select_can_id, 0));
//         sqlitew::reset(stmt_select_can_id);
//         sqlitew::clear_bindings(stmt_select_can_id);
//         return can_id;
//     };

//     auto find_prev = [&stmt_find_prev_row](uint32_t can_id, uint32_t idx, uint32_t& out_row) -> bool {
//         sqlitew::reset(stmt_find_prev_row);
//         sqlitew::clear_bindings(stmt_find_prev_row);
//         sqlitew::bind_int64(stmt_find_prev_row, 1, static_cast<sqlite3_int64>(can_id));
//         sqlitew::bind_int64(stmt_find_prev_row, 2, static_cast<sqlite3_int64>(idx));
//         if (sqlitew::step(stmt_find_prev_row) == SQLITE_ROW) {
//             out_row = static_cast<uint32_t>(sqlitew::column_int64(stmt_find_prev_row, 0));
//             sqlitew::reset(stmt_find_prev_row);
//             sqlitew::clear_bindings(stmt_find_prev_row);
//             return true;
//         }
//         sqlitew::reset(stmt_find_prev_row);
//         sqlitew::clear_bindings(stmt_find_prev_row);
//         return false;
//     };

//     auto find_next = [&stmt_find_next_row](uint32_t can_id, uint32_t idx, uint32_t& out_row) -> bool {
//         sqlitew::reset(stmt_find_next_row);
//         sqlitew::clear_bindings(stmt_find_next_row);
//         sqlitew::bind_int64(stmt_find_next_row, 1, static_cast<sqlite3_int64>(can_id));
//         sqlitew::bind_int64(stmt_find_next_row, 2, static_cast<sqlite3_int64>(idx));
//         if (sqlitew::step(stmt_find_next_row) == SQLITE_ROW) {
//             out_row = static_cast<uint32_t>(sqlitew::column_int64(stmt_find_next_row, 0));
//             sqlitew::reset(stmt_find_next_row);
//             sqlitew::clear_bindings(stmt_find_next_row);
//             return true;
//         }
//         sqlitew::reset(stmt_find_next_row);
//         sqlitew::clear_bindings(stmt_find_next_row);
//         return false;
//     };

//     auto update_changed_only = [&stmt_update_changed](uint32_t idx, bool changed) {
//         sqlitew::reset(stmt_update_changed);
//         sqlitew::clear_bindings(stmt_update_changed);
//         sqlitew::bind_int64(stmt_update_changed, 1, static_cast<sqlite3_int64>(changed ? 1 : 0));
//         sqlitew::bind_int64(stmt_update_changed, 2, static_cast<sqlite3_int64>(idx));
//         sqlitew::step(stmt_update_changed);
//         sqlitew::reset(stmt_update_changed);
//         sqlitew::clear_bindings(stmt_update_changed);
//     };

//     auto update_full_row = [&stmt_update_row](uint32_t idx, const LogRecord& rec, bool changed) {
//         const std::string channel = normalize_channel_key(rec.channel);
//         sqlitew::reset(stmt_update_row);
//         sqlitew::clear_bindings(stmt_update_row);
//         sqlitew::bind_double(stmt_update_row, 1, rec.timestamp);
//         sqlitew::bind_int64(stmt_update_row, 2, static_cast<sqlite3_int64>(rec.can_id));
//         sqlitew::bind_int64(stmt_update_row, 3, static_cast<sqlite3_int64>(rec.direction));
//         sqlitew::bind_text(stmt_update_row, 4, channel.c_str(), -1, nullptr);
//         sqlitew::bind_int64(stmt_update_row, 5, static_cast<sqlite3_int64>(changed ? 1 : 0));
//         sqlitew::bind_int64(stmt_update_row, 6, static_cast<sqlite3_int64>(idx));
//         sqlitew::step(stmt_update_row);
//         sqlitew::reset(stmt_update_row);
//         sqlitew::clear_bindings(stmt_update_row);
//     };

//     try {
//         sqlitew::prepare_v2(db_, kSelectCanIdSql, -1, &stmt_select_can_id, nullptr);
//         sqlitew::prepare_v2(db_, kFindPrevSql, -1, &stmt_find_prev_row, nullptr);
//         sqlitew::prepare_v2(db_, kFindNextSql, -1, &stmt_find_next_row, nullptr);
//         sqlitew::prepare_v2(db_, kUpdateChangedSql, -1, &stmt_update_changed, nullptr);
//         sqlitew::prepare_v2(db_, kUpdateRowSql, -1, &stmt_update_row, nullptr);

//         const uint32_t old_can_id = get_can_id(row_index);
//         const uint32_t new_can_id = entry.can_id;

//         uint32_t prev_new = 0;
//         uint32_t next_new = 0;
//         uint32_t prev_old = 0;
//         uint32_t next_old = 0;
//         const bool has_prev_new = find_prev(new_can_id, row_index, prev_new);
//         const bool has_next_new = find_next(new_can_id, row_index, next_new);
//         const bool has_prev_old = (old_can_id != new_can_id)
//             ? find_prev(old_can_id, row_index, prev_old)
//             : false;
//         const bool has_next_old = (old_can_id != new_can_id)
//             ? find_next(old_can_id, row_index, next_old)
//             : false;

//         bool changed_row = false;
//         if (has_prev_new) {
//             const LogRecord prev_payload = read_payload(prev_new);
//             changed_row = payload_changed(prev_payload, entry);
//         }

//         begin_transaction();
//         update_full_row(row_index, entry, changed_row);

//         if (has_next_new) {
//             const LogRecord next_payload = read_payload(next_new);
//             const bool changed_next_new = payload_changed(entry, next_payload);
//             update_changed_only(next_new, changed_next_new);
//         }

//         if (has_next_old) {
//             const LogRecord next_old_payload = read_payload(next_old);
//             bool changed_next_old = false;
//             if (has_prev_old) {
//                 const LogRecord prev_old_payload = read_payload(prev_old);
//                 changed_next_old = payload_changed(prev_old_payload, next_old_payload);
//             }
//             update_changed_only(next_old, changed_next_old);
//         }

//         commit_transaction();

//         if (stmt_update_row != nullptr) {
//             sqlitew::finalize(stmt_update_row);
//             stmt_update_row = nullptr;
//         }
//         if (stmt_update_changed != nullptr) {
//             sqlitew::finalize(stmt_update_changed);
//             stmt_update_changed = nullptr;
//         }
//         if (stmt_find_next_row != nullptr) {
//             sqlitew::finalize(stmt_find_next_row);
//             stmt_find_next_row = nullptr;
//         }
//         if (stmt_find_prev_row != nullptr) {
//             sqlitew::finalize(stmt_find_prev_row);
//             stmt_find_prev_row = nullptr;
//         }        
//         if (stmt_select_can_id != nullptr) {
//             sqlitew::finalize(stmt_select_can_id);
//             stmt_select_can_id = nullptr;
//         }    
//         if (opened_here && db_ != nullptr) {
//             sqlitew::close(db_);
//             db_ = nullptr;
//         }

//         return true;
//     } catch (...) {
//         std::exception_ptr proc_ex = std::current_exception();
//         // Cleanup; if cleanup throws, prefer that exception to propagate.
//             if (stmt_update_row != nullptr) { sqlitew::finalize(stmt_update_row); stmt_update_row = nullptr; }
//             if (stmt_update_changed != nullptr) { sqlitew::finalize(stmt_update_changed); stmt_update_changed = nullptr; }
//             if (stmt_find_next_row != nullptr) { sqlitew::finalize(stmt_find_next_row); stmt_find_next_row = nullptr; }
//             if (stmt_find_prev_row != nullptr) { sqlitew::finalize(stmt_find_prev_row); stmt_find_prev_row = nullptr; }
//             if (stmt_select_can_id != nullptr) { sqlitew::finalize(stmt_select_can_id); stmt_select_can_id = nullptr; }
//             if (opened_here && db_ != nullptr) { sqlitew::close(db_); db_ = nullptr; }
//         std::rethrow_exception(proc_ex);
//     }
// }
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
