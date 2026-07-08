#include "sqlite/log_index_db.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "can_analyzer_log.h"
#include "mmap/mmap_data.h"

// Use centralized SQLite ABI declarations.
#include "sqlite/sqlite_wrapper.h"
#include <stdexcept>

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

std::pair<int64_t, int64_t> to_page_window(int64_t first, int64_t last) {
    if (last < first) {
        return {first, 0};
    }
    return {first, (last - first) + 1};
}

} // namespace

namespace file_service {

LogIndexDatabase::LogIndexDatabase(const std::string& token_path)
    : db_path_(token_path + ".index.sqlite") {}

LogIndexDatabase::~LogIndexDatabase() {
    close();
}

std::string LogIndexDatabase::db_path() const {
    return db_path_;
}

void LogIndexDatabase::ensure_open() const {
    if (db_ == nullptr) {
        throw std::runtime_error("LogIndexDatabase used before open() or after close()");
    }
}

const std::string& LogIndexDatabase::last_error_message() const {
    return last_error_message_;
}

void LogIndexDatabase::open() {
    if (db_ != nullptr) {
        return;
    }

    static const char* kInsertSql =
        "INSERT OR REPLACE INTO log_index "
        "(row_index, timestamp, can_id, direction, channel, changed) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlitew::open(db_path_.c_str(), &db_);
    stmt_ = std::make_unique<sqlitew::Stmt>(db_, kInsertSql);

    last_error_message_.clear();
    sqlitew::exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlitew::exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlitew::exec(db_, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
}

void LogIndexDatabase::close() {
    stmt_.reset();
    last_raw_by_id_.clear();
    if (db_ != nullptr) {
        sqlitew::close(db_);
        db_ = nullptr;
    }
}

bool LogIndexDatabase::compute_changed_and_update(uint32_t can_id,
                                                  const uint8_t* data,
                                                  uint8_t data_len) {
    const uint8_t bounded_len = (data_len <= static_cast<uint8_t>(sizeof(PrevRaw::data)))
        ? data_len
        : static_cast<uint8_t>(sizeof(PrevRaw::data));

    auto it = last_raw_by_id_.find(can_id);
    if (it == last_raw_by_id_.end()) {
        PrevRaw prev{};
        prev.len = bounded_len;
        if (bounded_len > 0) {
            std::memcpy(prev.data, data, bounded_len);
        }
        last_raw_by_id_.emplace(can_id, prev);
        return false;
    }

    const PrevRaw& prev = it->second;
    const bool changed = (prev.len != bounded_len)
        || (bounded_len > 0 && std::memcmp(prev.data, data, bounded_len) != 0);

    it->second.len = bounded_len;
    if (bounded_len > 0) {
        std::memcpy(it->second.data, data, bounded_len);
    }
    return changed;
}

int32_t LogIndexDatabase::initialize_schema() {
    ensure_open();

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
    last_error_message_.clear();
    return 0;
}

int32_t LogIndexDatabase::begin_transaction() {
    ensure_open();
    sqlitew::exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    last_error_message_.clear();
    return 0;
}

int32_t LogIndexDatabase::commit_transaction() {
    ensure_open();
    sqlitew::exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    last_error_message_.clear();
    return 0;
}

// Adding context data like row_index or changed
void LogIndexDatabase::append_entry(
                                    uint32_t row_index,
                                    const LogRecord& entry)
{
    ensure_open();
    if (!stmt_) {
        throw std::runtime_error("LogIndexDatabase statement is not initialized");
    }

    const bool changed = compute_changed_and_update(entry.can_id, entry.data, entry.data_len);

    const std::string channel =
        file_service::mmap::DataMmapInterface::normalize_channel_key(entry.channel);

    stmt_->bind_int64(1, row_index);
    stmt_->bind_double(2, entry.timestamp);
    stmt_->bind_int64(3, entry.can_id);
    stmt_->bind_int64(4, entry.direction);
    stmt_->bind_text(5, channel.c_str(), -1, nullptr);
    stmt_->bind_int64(6, changed ? 1 : 0);

    stmt_->step();

    stmt_->reset();
    stmt_->clear_bindings();
}

void LogIndexDatabase::update_entry(uint32_t row_index,
                                    const LogRecord& entry,
                                    const file_service::mmap::DataMmapInterface& data) {
    ensure_open();

    auto read_payload = [&data](uint64_t idx) -> LogRecord {
        ParsedEntry parsed{};
        data.read_entry(idx, parsed);
        return static_cast<const LogRecord&>(parsed);
    };

    auto payload_changed = [](const LogRecord& prev, const LogRecord& cur) -> bool {
        const uint8_t prev_len = (prev.data_len <= sizeof(prev.data))
            ? prev.data_len
            : static_cast<uint8_t>(sizeof(prev.data));
        const uint8_t cur_len = (cur.data_len <= sizeof(cur.data))
            ? cur.data_len
            : static_cast<uint8_t>(sizeof(cur.data));
        if (prev_len != cur_len) {
            return true;
        }
        return cur_len > 0 && std::memcmp(prev.data, cur.data, cur_len) != 0;
    };

    auto get_can_id = [this](uint32_t idx) -> uint32_t {
        sqlitew::Stmt stmt(db_, "SELECT can_id FROM log_index WHERE row_index = ? LIMIT 1;");
        stmt.bind_int64(1, static_cast<sqlite3_int64>(idx));
        if (stmt.step() != SQLITE_ROW) {
            throw std::runtime_error("update_entry target row not found in log_index");
        }
        return static_cast<uint32_t>(stmt.column_int64(0));
    };

    auto find_prev = [this](uint32_t can_id, uint32_t idx, uint32_t& out_row) -> bool {
        sqlitew::Stmt stmt(db_,
            "SELECT row_index FROM log_index "
            "WHERE can_id = ? AND row_index < ? "
            "ORDER BY row_index DESC LIMIT 1;");
        stmt.bind_int64(1, static_cast<sqlite3_int64>(can_id));
        stmt.bind_int64(2, static_cast<sqlite3_int64>(idx));
        if (stmt.step() == SQLITE_ROW) {
            out_row = static_cast<uint32_t>(stmt.column_int64(0));
            return true;
        }
        return false;
    };

    auto find_next = [this](uint32_t can_id, uint32_t idx, uint32_t& out_row) -> bool {
        sqlitew::Stmt stmt(db_,
            "SELECT row_index FROM log_index "
            "WHERE can_id = ? AND row_index > ? "
            "ORDER BY row_index ASC LIMIT 1;");
        stmt.bind_int64(1, static_cast<sqlite3_int64>(can_id));
        stmt.bind_int64(2, static_cast<sqlite3_int64>(idx));
        if (stmt.step() == SQLITE_ROW) {
            out_row = static_cast<uint32_t>(stmt.column_int64(0));
            return true;
        }
        return false;
    };

    auto update_changed_only = [this](uint32_t idx, bool changed) {
        sqlitew::Stmt stmt(db_, "UPDATE log_index SET changed = ? WHERE row_index = ?;");
        stmt.bind_int64(1, static_cast<sqlite3_int64>(changed ? 1 : 0));
        stmt.bind_int64(2, static_cast<sqlite3_int64>(idx));
        stmt.step();
    };

    auto update_full_row = [this](uint32_t idx, const LogRecord& rec, bool changed) {
        const std::string channel = file_service::mmap::DataMmapInterface::normalize_channel_key(rec.channel);
        sqlitew::Stmt stmt(db_,
            "UPDATE log_index "
            "SET timestamp = ?, can_id = ?, direction = ?, channel = ?, changed = ? "
            "WHERE row_index = ?;");
        stmt.bind_double(1, rec.timestamp);
        stmt.bind_int64(2, static_cast<sqlite3_int64>(rec.can_id));
        stmt.bind_int64(3, static_cast<sqlite3_int64>(rec.direction));
        stmt.bind_text(4, channel.c_str(), -1, nullptr);
        stmt.bind_int64(5, static_cast<sqlite3_int64>(changed ? 1 : 0));
        stmt.bind_int64(6, static_cast<sqlite3_int64>(idx));
        stmt.step();
    };

    const uint32_t old_can_id = get_can_id(row_index);
    const uint32_t new_can_id = entry.can_id;

    uint32_t prev_new = 0;
    uint32_t next_new = 0;
    uint32_t prev_old = 0;
    uint32_t next_old = 0;
    const bool has_prev_new = find_prev(new_can_id, row_index, prev_new);
    const bool has_next_new = find_next(new_can_id, row_index, next_new);
    const bool has_prev_old = (old_can_id != new_can_id)
        ? find_prev(old_can_id, row_index, prev_old)
        : false;
    const bool has_next_old = (old_can_id != new_can_id)
        ? find_next(old_can_id, row_index, next_old)
        : false;

    bool changed_row = false;
    if (has_prev_new) {
        const LogRecord prev_payload = read_payload(prev_new);
        changed_row = payload_changed(prev_payload, entry);
    }

    begin_transaction();
    update_full_row(row_index, entry, changed_row);

    if (has_next_new) {
        const LogRecord next_payload = read_payload(next_new);
        const bool changed_next_new = payload_changed(entry, next_payload);
        update_changed_only(next_new, changed_next_new);
    }

    if (has_next_old) {
        const LogRecord next_old_payload = read_payload(next_old);
        bool changed_next_old = false;
        if (has_prev_old) {
            const LogRecord prev_old_payload = read_payload(prev_old);
            changed_next_old = payload_changed(prev_old_payload, next_old_payload);
        }
        update_changed_only(next_old, changed_next_old);
    }

    commit_transaction();
}

// int32_t LogIndexDatabase::append_entries(const std::vector<EntryUpdate>& entries,
//                                          uint64_t start_row_index,
//                                          const std::unordered_set<uint32_t>& changed_rows) {
//     ensure_open();
//     if (entries.empty()) {
//         return 0;
//     }

//     static const char* kInsertSql =
//         "INSERT OR REPLACE INTO log_index "
//         "(row_index, timestamp, can_id, direction, channel, changed) "
//         "VALUES (?, ?, ?, ?, ?, ?);";

//     sqlitew::Stmt stmt(db_, kInsertSql);

//     for (const auto& entry : entries)
//     {   
//         append_entry(
//                 stmt,
//                 entry.row_index,
//                 entry.record,
//                 changed_rows.);
//     }

//     // `stmt` is RAII and will be finalized in its destructor.
//     last_error_message_.clear();
//     return 0;
// }

// int32_t LogIndexDatabase::update_entries(const std::vector<EntryUpdate>& entries) {
//     ensure_open();
//     if (entries.empty()) {
//         return 0;
//     }

//     static const char* kUpdateSql =
//         "UPDATE log_index "
//         "SET timestamp = ?, can_id = ?, direction = ?, channel = ?, changed = ? "
//         "WHERE row_index = ?;";

//     sqlitew::Stmt stmt(db_, kUpdateSql);

//     begin_transaction();

//     for (const EntryUpdate& e : entries) {
//         // EntryUpdate.row_index is 0-based and directly used as row_index
//         const uint64_t row_index = static_cast<uint64_t>(e.row_index);

//         const std::string channel = file_service::mmap::DataMmapInterface::normalize_channel_key(e.record.channel);
//         const int changed = 0; // changed is computed server-side; index caller should provide correct rows via EntryUpdate

//         stmt.bind_double(1, e.record.timestamp);
//         stmt.bind_int64(2, static_cast<sqlite3_int64>(e.record.can_id));
//         stmt.bind_int64(3, static_cast<sqlite3_int64>(e.record.direction));
//         stmt.bind_text(4, channel.c_str(), -1, nullptr);
//         stmt.bind_int64(5, static_cast<sqlite3_int64>(changed));
//         stmt.bind_int64(6, static_cast<sqlite3_int64>(row_index));

//         stmt.step();

//         stmt.reset();
//         stmt.clear_bindings();
//     }

//     // `stmt` is RAII and will be finalized in its destructor.

//     commit_transaction();

//     last_error_message_.clear();
//     return 0;
// }

std::vector<uint64_t> LogIndexDatabase::query_row_indices(const LogQuery& query,
                                                          int64_t first,
                                                          int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    if (db_ == nullptr || page_size <= 0) {
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

    sqlitew::Stmt stmt(db_, sql.c_str());

    int bind_idx = 1;
    for (const uint32_t can_id : query.can_ids) {
        stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(can_id));
    }
    for (const uint8_t direction : query.directions) {
        stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(direction));
    }
    for (const std::string& channel : query.channels) {
        stmt.bind_text(bind_idx++, channel.c_str(), -1, nullptr);
    }
    if (query.has_time_range) {
        stmt.bind_double(bind_idx++, query.first_ts);
        stmt.bind_double(bind_idx++, query.last_ts);
    }
    stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(page_size));   // LIMIT
    stmt.bind_int64(bind_idx++, static_cast<sqlite3_int64>(first_line));  // OFFSET

    std::vector<uint64_t> rows;
    rows.reserve(static_cast<size_t>(page_size));
    while (stmt.step() == SQLITE_ROW) {
        rows.push_back(static_cast<uint64_t>(stmt.column_int64(0)));
    }

    // `stmt` is RAII and will be finalized in its destructor.
    last_error_message_.clear();
    return rows;
}

} // namespace file_service
