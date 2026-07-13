#include "sqlite/log_index_db.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "can_analyzer_log.h"

// Use centralized SQLite ABI declarations.
#include "sqlite/sqlite_wrapper.h"
#include <stdexcept>
#include <exception>

namespace {

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;

// constexpr int32_t kLogIndexRcOpenFailed        = -401;
// constexpr int32_t kLogIndexRcSchemaDbClosed     = -402;
// constexpr int32_t kLogIndexRcSchemaExecFailed   = -403;
// constexpr int32_t kLogIndexRcBeginDbClosed      = -404;
// constexpr int32_t kLogIndexRcBeginExecFailed    = -405;
// constexpr int32_t kLogIndexRcCommitDbClosed     = -406;
// constexpr int32_t kLogIndexRcCommitExecFailed   = -407;
// constexpr int32_t kLogIndexRcWriteDbClosed      = -408;
// constexpr int32_t kLogIndexRcWritePrepareFailed = -409;
// constexpr int32_t kLogIndexRcWriteStepFailed    = -410;
// constexpr int32_t kLogIndexRcUpdatePrepareFailed = -411;
// constexpr int32_t kLogIndexRcUpdateStepFailed    = -412;
// constexpr int32_t kLogIndexRcRollbackExecFailed  = -413;
// constexpr int32_t kLogIndexRcInvalidLineNumber   = -414;

std::string format_sqlite_error(sqlite3* db, const char* op, int rc) {
    return std::string(op) + " rc=" + std::to_string(rc) + " sqlite_msg=" +
        (db != nullptr ? sqlitew::errmsg(db) : "<null-db>");
}

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
    : db_path_(std::move(db_path)) {}

LogIndexDatabase::~LogIndexDatabase() {
    //close();
}

std::string LogIndexDatabase::db_path() const {
    return db_path_;
}

// const std::string& LogIndexDatabase::last_error_message() const {
//     return last_error_message_;
// }

void LogIndexDatabase::open_append_session() {
    if (db_ != nullptr && stmt_ != nullptr) {
        return;
    }

    static const char* kInsertSql =
        "INSERT OR REPLACE INTO log_index "
        "(row_index, timestamp, can_id, direction, channel, changed) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlitew::open(db_path_.c_str(), &db_);

    /// 20260709 BUG: By default, SQLite uses DELETE journal mode. -> can not do 1 writer, multiple readers
    sqlitew::exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlitew::exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlitew::exec(db_, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);

    // Ensure the schema is present before preparing statements that depend on it.
    initialize_schema();

    if (stmt_ == nullptr) {
        sqlitew::prepare_v2(db_, kInsertSql, -1, &stmt_, nullptr);
    }

    //last_error_message_.clear();
}

void LogIndexDatabase::close_append_session() {
    if (stmt_ != nullptr) {
        sqlitew::finalize(stmt_);
        stmt_ = nullptr;
    }
    last_raw_by_id_.clear();
    if (db_ != nullptr) {
        sqlitew::close(db_);
        db_ = nullptr;
    }
}

// bool LogIndexDatabase::compute_changed_and_update(uint32_t can_id,
//                                                   const uint8_t* data,
//                                                   uint8_t data_len) {
//     const uint8_t bounded_len = (data_len <= static_cast<uint8_t>(sizeof(PrevRaw::data)))
//         ? data_len
//         : static_cast<uint8_t>(sizeof(PrevRaw::data));

//     auto it = last_raw_by_id_.find(can_id);
//     if (it == last_raw_by_id_.end()) {
//         PrevRaw prev{};
//         prev.len = bounded_len;
//         if (bounded_len > 0) {
//             std::memcpy(prev.data, data, bounded_len);
//         }
//         last_raw_by_id_.emplace(can_id, prev);
//         return false;
//     }

//     const PrevRaw& prev = it->second;
//     const bool changed = (prev.len != bounded_len)
//         || (bounded_len > 0 && std::memcmp(prev.data, data, bounded_len) != 0);

//     it->second.len = bounded_len;
//     if (bounded_len > 0) {
//         std::memcpy(it->second.data, data, bounded_len);
//     }
//     return changed;
// }

int32_t LogIndexDatabase::initialize_schema() {
    // row_index is the rowid (payload row in the mmap data store).
    // Filtering columns are indexed; changed uses a small partial index.
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

    sqlitew::exec(db_, kSchemaSql, nullptr, nullptr, nullptr);
    //last_error_message_.clear();
    return 0;
}

int32_t LogIndexDatabase::begin_transaction() {
    sqlitew::exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    //last_error_message_.clear();
    return 0;
}

int32_t LogIndexDatabase::commit_transaction() {
    sqlitew::exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    // last_error_message_.clear();
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
    if (db_ == nullptr) {
        throw std::runtime_error("LogIndexDatabase used before open() or after close()");
    }
    if (!stmt_) {
        throw std::runtime_error("LogIndexDatabase statement is not initialized");
    }

    const std::string channel = normalize_channel_key(entry.channel);

    sqlitew::bind_int64(stmt_, 1, row_index);
    sqlitew::bind_double(stmt_, 2, entry.timestamp);
    sqlitew::bind_int64(stmt_, 3, entry.can_id);
    sqlitew::bind_int64(stmt_, 4, entry.direction);
    sqlitew::bind_text(stmt_, 5, channel.c_str(), -1, nullptr);
    sqlitew::bind_int64(stmt_, 6, static_cast<sqlite3_int64>(entry.changed ? 1 : 0));

    sqlitew::step(stmt_);

    sqlitew::reset(stmt_);
    sqlitew::clear_bindings(stmt_);
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
    bool opened_here = false;

    if (db_ == nullptr)
    {
        sqlitew::open(db_path_.c_str(), &db_);
        opened_here = true;
    }

    static const char* kUpdateSql =
        "UPDATE log_index "
        "SET timestamp = ?, "
        "    can_id = ?, "
        "    direction = ?, "
        "    channel = ?, "
        "    changed = ? "
        "WHERE row_index = ?;";

    sqlite3_stmt* stmt = nullptr;

    try
    {
        sqlitew::prepare_v2(db_, kUpdateSql, -1, &stmt, nullptr);

        const std::string channel = normalize_channel_key(entry.channel);

        sqlitew::bind_double(stmt, 1, entry.timestamp);
        sqlitew::bind_int64(stmt, 2, static_cast<sqlite3_int64>(entry.can_id));
        sqlitew::bind_int64(stmt, 3, static_cast<sqlite3_int64>(entry.direction));
        sqlitew::bind_text(stmt, 4, channel.c_str(), -1, nullptr);
        sqlitew::bind_int64(stmt, 5, static_cast<sqlite3_int64>(entry.changed));
        sqlitew::bind_int64(stmt, 6, static_cast<sqlite3_int64>(row_index));

        sqlitew::step(stmt);

        sqlitew::finalize(stmt);

        if (opened_here)
        {
            sqlitew::close(db_);
            db_ = nullptr;
        }

        return true;
    }
    catch (...)
    {
        if (stmt)
        {
            sqlitew::finalize(stmt);
        }

        if (opened_here && db_)
        {
            sqlitew::close(db_);
            db_ = nullptr;
        }

        throw;
    }
}



std::vector<uint32_t> LogIndexDatabase::query_row_indices(const LogQuery& query,
                                                          int32_t first,
                                                          int32_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    if (page_size <= 0) {
        return {};
    }

    bool opened_here = false;
    if (db_ == nullptr) {
        /// NOTE: By default, SQLite uses DELETE journal mode. -> can not do 1 writer, multiple readers
        sqlitew::open(db_path_.c_str(), &db_);
        sqlitew::exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlitew::exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlitew::exec(db_, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
        opened_here = true;
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

    sqlite3_stmt* stmt = nullptr;
    try {
        sqlitew::prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int bind_idx = 1;
        for (const uint32_t can_id : query.can_ids) {
            sqlitew::bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(can_id));
        }
        for (const uint8_t direction : query.directions) {
            sqlitew::bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(direction));
        }
        for (const std::string& channel : query.channels) {
            sqlitew::bind_text(stmt, bind_idx++, channel.c_str(), -1, nullptr);
        }
        if (query.has_time_range) {
            sqlitew::bind_double(stmt, bind_idx++, query.first_ts);
            sqlitew::bind_double(stmt, bind_idx++, query.last_ts);
        }
        sqlitew::bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(page_size));   // LIMIT
        sqlitew::bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(first_line));  // OFFSET

        std::vector<uint32_t> rows;
        rows.reserve(static_cast<size_t>(page_size));
        while (sqlitew::step(stmt) == SQLITE_ROW) {
            rows.push_back(static_cast<uint32_t>(sqlitew::column_int64(stmt, 0)));
        }

        if (stmt != nullptr) { sqlitew::finalize(stmt); stmt = nullptr; }
        if (opened_here && db_ != nullptr) { sqlitew::close(db_); db_ = nullptr; }
        //last_error_message_.clear();
        return rows;
    } catch (...) {
        std::exception_ptr proc_ex = std::current_exception();
            if (stmt != nullptr) { sqlitew::finalize(stmt); stmt = nullptr; }
            if (opened_here && db_ != nullptr) { sqlitew::close(db_); db_ = nullptr; }
        std::rethrow_exception(proc_ex);
    }
    }


bool LogIndexDatabase::get_first_last_timestamp(double& out_first_ts,
                                                             double& out_last_ts) const {
    out_first_ts = 0.0;
    out_last_ts = 0.0;

    sqlite3* ldb = nullptr;
    sqlite3_stmt* stmt = nullptr;
    static const char* kSql = "SELECT COUNT(1), MIN(timestamp), MAX(timestamp) FROM log_index;";
    try {
        /// NOTE: By default, SQLite uses DELETE journal mode. -> can not do 1 writer, multiple readers
        sqlitew::open(db_path_.c_str(), &ldb);
        sqlitew::exec(ldb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlitew::exec(ldb, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlitew::exec(ldb, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
        sqlitew::prepare_v2(ldb, kSql, -1, &stmt, nullptr);

        const int step_rc = sqlitew::step(stmt);

        const sqlite3_int64 count = sqlitew::column_int64(stmt, 0);
        if (count == 0) {
            sqlitew::finalize(stmt);
            sqlitew::close(ldb);
            return false;
        }

        out_first_ts = sqlitew::column_double(stmt, 1);
        out_last_ts = sqlitew::column_double(stmt, 2);

        sqlitew::finalize(stmt);
        stmt = nullptr;
        sqlitew::close(ldb);
        ldb = nullptr;
        return true;
    } catch (...) {
        if (stmt != nullptr) { sqlitew::finalize(stmt); stmt = nullptr; }
        if (ldb != nullptr) { sqlitew::close(ldb); ldb = nullptr; }
        throw;
    }
}

uint32_t LogIndexDatabase::row_count() const {
    sqlite3* ldb = nullptr;
    sqlite3_stmt* stmt = nullptr;
    static const char* kSql = "SELECT COUNT(1) FROM log_index;";
    try {
        sqlitew::open(db_path_.c_str(), &ldb);
        sqlitew::prepare_v2(ldb, kSql, -1, &stmt, nullptr);

        const int step_rc = sqlitew::step(stmt);

        const sqlite3_int64 count = sqlitew::column_int64(stmt, 0);

        sqlitew::finalize(stmt);
        stmt = nullptr;
        sqlitew::close(ldb);
        ldb = nullptr;
        return static_cast<uint32_t>(count);
    } catch (...) {
        if (stmt != nullptr) { sqlitew::finalize(stmt); stmt = nullptr; }
        if (ldb != nullptr) { sqlitew::close(ldb); ldb = nullptr; }
        throw;
    }
}
