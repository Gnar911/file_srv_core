#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mmap/mmap_data.h"
#include "parsed_entry_layout.h"
#include "sqlite/log_index_db.h"
#include "storage_token.h"
#include "meta_data_tracker.h"


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

/// @brief 20260709 
///             1. This class is only for display the database with the statistics or context metadata add in for analysis
/// 				It is not supposed to be used for high throughput read for execution which no need the context metadata like changed, last timestamp,...
///				2. The class will open RAII on Index + MMap once it created, thus all the quiry + append need to update and sync both storages
class MetaDataStorageInterface {
public:
	explicit MetaDataStorageInterface(std::string token_id);

	struct Metadata {
		double first_timestamp = 0.0;
		double last_timestamp = 0.0;
		uint32_t total_rows = 0;
		std::string source_file_path; // stored by set_source_file_path when parsing
	};

	Metadata get_metadata() const;

	void open_storage();
	void set_file_path(const std::string& path);
	void write_entries(const std::vector<LogRecord>& entries);
	void update_entry(uint32_t row_index,
                      const LogRecord& entry);
	void close_storage();

	std::vector<ParsedEntry> read_page(int32_t first, int32_t last) const;
	std::vector<ParsedEntry> read_page_multi(const LogQuery& query,
	                                         int32_t first,
	                                         int32_t last);
	bool get_first_last_timestamp(double& out_first_ts,
							 double& out_last_ts) const;		

	uint32_t fetch_count() const;
	const std::string& token_path() const;
	//int32_t last_error_code() const;

private:
	friend class MetaDataStorageInterfaceTestAccessor;
	//void clear_last_error() const;

	std::string token_id;
	StorageToken storage_token_{""};
	mmap::DataMmapInterface<mmap::Access::ReadWrite> wdata_;
	mmap::DataMmapInterface<mmap::Access::ReadOnly> rdata_;
	LogIndexDatabase index_db_;
	MetadataTracker tracker_;
	bool initialized_ = false;
	mutable int32_t last_error_code_ = 0;
};

