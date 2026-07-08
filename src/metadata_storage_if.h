#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mmap/mmap_data.h"
#include "parsed_entry_layout.h"
#include "sqlite/log_index_db.h"

namespace file_service {

// ─────────────────────────────────────────────────────────────────────────────
// MetaDataStorageInterface — storage-technique-agnostic facade for parsed CAN
// logs. It hides HOW a parsed log is stored/queried behind one interface, and
// deliberately combines two engines, each used for what it is best at:
//
//   mmap  (DataMmapInterface) = PAYLOAD store / source of truth.
//         Fixed-size LogRecord => sequential paging is pointer+offset, random
//         access is O(1), the multithreaded parser writes one contiguous dump,
//         and pages are handed to pybind zero-copy. read_page / read_all_entries
//         / read_rows_from_data stay on mmap.
//
//   SQLite (LogIndexDatabase) = FILTER/INDEX engine ONLY.
//         Multi-factor predicates (can_id AND direction AND channel AND changed
//         AND time-range) blow up a per-factor mmap index design
//         combinatorially; a SQL WHERE with B-tree indexes answers them in one
//         query. SQLite stores only the small filter columns + row_index, never
//         the payload.
//
//   FLOW:
//     parse ─► write payload  ─► DATA MMAP        (source of truth)
//           └► write metadata ─► SQLite log_index (can_id, direction, channel,
//                                                  changed, timestamp, row_index)
//
//     query(multi-factor) ─► SQLite: SELECT row_index WHERE ... LIMIT/OFFSET
//                         └► row indices ─► read_rows_from_data() ─► ParsedEntry[]
//
//   HISTORY: this used to be ParsedMmapInterface in mmap/, and it also built
//   per-factor mmap index families (can_id/channel/direction). Those are gone;
//   filtering now lives entirely in SQLite via the single read_page_multi()
//   entry point (which superseded read_page_from_can_id(s)/_changed/
//   _channel(s)/_direction(s)). The old mmap index sources remain on disk but
//   are no longer part of the build.
// ─────────────────────────────────────────────────────────────────────────────

class MetaDataStorageInterface {
public:
	explicit MetaDataStorageInterface(std::string mmap_prefix);

	void open_storage();
	void write_entries(const std::vector<LogRecord>& entries);
	void update_entries(const std::vector<EntryUpdate>& entries);
	void set_file_path(std::string file_path);
	std::string get_file_path() const;
	void close_storage();

	// Payload reads (mmap, zero-copy friendly).
	std::vector<ParsedEntry> read_page(int64_t first, int64_t last) const;
	std::vector<ParsedEntry> read_all_entries() const;

	// Unified multi-factor query (SQLite filter -> mmap payload fetch).
	// Combine any of can_ids / channels / directions / changed / time-range in
	// one LogQuery. [first, last] is the inclusive page window into the FILTERED
	// result. Replaces the removed single-factor read_page_from_* methods.
	std::vector<ParsedEntry> read_page_multi(const LogQuery& query,
	                                         int64_t first,
	                                         int64_t last);

	void get_first_last_timestamp(double& out_first_ts,
	                             double& out_last_ts) const;

	uint64_t fetch_count() const;
	const std::string& token_path() const;
	int32_t last_error_code() const;

private:
	friend class MetaDataStorageInterfaceTestAccessor;

	std::vector<std::string> data_segment_paths() const;

	bool is_segment_writers_ready() const;
	void reset_runtime_only();
	void clear_last_error() const;
	std::vector<ParsedEntry> read_rows_from_data(const std::vector<uint64_t>& rows) const;

	std::string mmap_prefix_;
	file_service::mmap::DataMmapInterface data_;
	file_service::LogIndexDatabase index_db_;
	bool initialized_ = false;
	mutable int32_t last_error_code_ = 0;
};

} // namespace file_service
