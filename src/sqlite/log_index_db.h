#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// log_index_db.h  —  SQLite-backed multi-factor row filter for parsed CAN logs
// ─────────────────────────────────────────────────────────────────────────────
//
// WHY THIS EXISTS (design decision — keep for future maintainers):
//
//   The parsed CAN payload is a FIXED-SIZE record (LogRecord, ~94 bytes). That
//   shape is ideal for mmap: sequential paging is a pointer + offset, random
//   access is O(1), bulk writes from the multithreaded parser are one big
//   contiguous dump, and entries are handed to pybind zero-copy. We keep all of
//   that: the mmap data store (DataMmapInterface) remains the SOURCE OF TRUTH
//   for the payload.
//
//   The problem was FILTERING. The old design built one physical mmap index
//   family per factor (can_id / channel / direction) and merged segments by
//   hand (see mmap_canid_idx.cpp). Supporting *combinations*
//     (can_id ∈ {..} AND direction = Rx AND channel ∈ {..} AND changed)
//   would require either a pre-built index per factor combination (2^N families)
//   or an expensive runtime intersection of several heap merges. That does not
//   scale and is painful to maintain.
//
//   SQLite is a query planner with B-tree indexes: it composes arbitrary
//   AND/OR/range predicates with paging for free. So we use it ONLY as the
//   index/filter engine, storing just the small filterable columns plus the
//   row_index. It never holds the CAN payload.
//
//   FLOW:
//     parse ─► write payload  ─► DATA MMAP        (unchanged, source of truth)
//           └► write metadata ─► SQLite log_index (can_id, direction, channel,
//                                                  changed, timestamp, row_index)
//
//     query(multi-factor) ─► SQLite: SELECT row_index WHERE ... LIMIT/OFFSET
//                         └► row indices ─► DataMmapInterface.read_rows_from_data()
//                                        ─► ParsedEntry[]   (from MMAP)
//
//   Net: mmap for data (fast, zero-copy), SQLite for filtering (flexible).
//   This replaces read_page_from_can_id(s)/_changed/_channel(s)/_direction(s)
//   with a single read_page_multi(LogQuery, first, last).
//
// SECURITY: every user/file-derived value (channel text, can_ids, directions,
//   timestamps) is bound as a statement parameter. SQL text is built only from
//   fixed column names and the *count* of "?" placeholders — never by
//   concatenating values — so this is not injectable.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "parsed_entry_layout.h"
#include "sqlite/sqlite_wrapper.h"

struct sqlite3;
namespace file_service { namespace mmap { class DataMmapInterface; } }

namespace file_service {

// Generic index DB error return for callers that still use int32_t rc-style
// APIs. The implementation throws more specific exceptions; callers that need
// to remain rc-based can catch exceptions and return this code.
inline constexpr int32_t kLogIndexRcException = -400;


// Multi-factor filter. Within a single field the values are OR'd (SQL IN);
// across fields they are AND'd. An empty vector / disabled flag means "any".
struct LogQuery {
    std::vector<uint32_t>    can_ids;        // OR within; empty = any
    std::vector<std::string> channels;       // OR within; empty = any (normalized)
    std::vector<uint8_t>     directions;      // OR within; empty = any (raw LogRecord.direction)
    bool   changed_only  = false;             // true => only rows flagged changed
    bool   has_time_range = false;            // true => apply [first_ts, last_ts]
    double first_ts      = 0.0;
    double last_ts       = 0.0;
};

// SQLite index over the parsed rows. Stores only the filterable columns and the
// row_index that maps 1:1 to the mmap data store. Mirrors the style of
// DecodedSignalDatabase (sql_decode_if.h).
class LogIndexDatabase {
public:
    explicit LogIndexDatabase(const std::string& token_path);
    ~LogIndexDatabase();

    LogIndexDatabase(const LogIndexDatabase&) = delete;
    LogIndexDatabase& operator=(const LogIndexDatabase&) = delete;

    void open();
    void    close();
    std::string db_path() const;

    int32_t initialize_schema();
    int32_t begin_transaction();
    int32_t commit_transaction();

    void append_entry(uint32_t row_index,
                      const LogRecord& entry);
    void update_entry(uint32_t row_index,
                      const LogRecord& entry,
                      const file_service::mmap::DataMmapInterface& data);

    // Append one write batch to the index. row_index for entries[i] is
    // start_row_index + i. changed_rows holds the GLOBAL row indices that the
    // data writer flagged as changed (from IndexBuckets::can_id_changed_rows),
    // so the SQLite "changed" column stays consistent with the mmap indexes.
    //int32_t append_entries(const std::vector<LogRecord>& entries,
                        //    uint64_t start_row_index,
                        //    const std::unordered_set<uint32_t>& changed_rows);

    // Update existing rows (by trusted EntryUpdate.row_index -> LogRecord)
    // inside one SQLite transaction: BEGIN; UPDATE ...; UPDATE ...; COMMIT;
    //int32_t update_entries(const std::vector<EntryUpdate>& entries);

    // Multi-factor query. Returns the matching row indices for the requested
    // page (ordered by row_index). [first, last] is the inclusive window into
    // the FILTERED result, matching the old read_page_* paging semantics.
    std::vector<uint64_t> query_row_indices(const LogQuery& query,
                                            int64_t first,
                                            int64_t last);

    const std::string& last_error_message() const;

private:
    struct PrevRaw {
        uint8_t len = 0;
        uint8_t data[64] = {0};
    };

    void ensure_open() const;
    bool compute_changed_and_update(uint32_t can_id,
                                    const uint8_t* data,
                                    uint8_t data_len);
    /*
    NOTE: 20260706
    Raw pointer
    sqlitew::Stmt* stmt = new sqlitew::Stmt(...);

    // ...

    delete stmt;

    Problems:

    Forget delete → memory leak.
    Delete twice → crash.
    Exception before delete → leak.    
    */
    std::unique_ptr<sqlitew::Stmt> stmt_;
    sqlite3* db_ = nullptr;
    std::unordered_map<uint32_t, PrevRaw> last_raw_by_id_;
    std::string db_path_;
    std::string last_error_message_;
};

} // namespace file_service
