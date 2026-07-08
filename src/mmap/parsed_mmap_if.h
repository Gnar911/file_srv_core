#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mmap/mmap_canid_idx.h"
#include "mmap/mmap_chnl_idx.h"
#include "mmap/mmap_data.h"
#include "mmap/mmap_dir_idx.h"
#include "parsed_entry_layout.h"
#include "sqlite/log_index_db.h"

namespace file_service {

// ─────────────────────────────────────────────────────────────────────────────
// STORAGE vs FILTERING split (design decision — keep for future maintainers):
//
//   mmap  = payload store (source of truth). Fixed-size LogRecord => sequential
//           paging is pointer+offset, random access is O(1), the multithreaded
//           parser writes one contiguous dump, and pages are handed to pybind
//           zero-copy. read_page / read_rows_from_data stay on mmap.
//
//   SQLite = filter/index engine ONLY. Multi-factor predicates
//           (can_id AND direction AND channel AND changed AND time-range) blow
//           up the old per-factor mmap index families combinatorially; a SQL
//           WHERE with B-tree indexes handles them in one query. SQLite stores
//           just the small filter columns + row_index, never the payload.
//
//   FLOW:
//     parse ─► write payload  ─► DATA MMAP        (unchanged, source of truth)
//           └► write metadata ─► SQLite log_index (can_id, direction, channel,
//                                                  changed, timestamp, row_index)
//
//     query(multi-factor) ─► SQLite: SELECT row_index WHERE ... LIMIT/OFFSET
//                         └► row indices ─► read_rows_from_data() ─► ParsedEntry[]
//
//   read_page_multi(LogQuery, first, last) is the single entry point that
//   supersedes read_page_from_can_id(s)/_changed/_channel(s)/_direction(s).
// ─────────────────────────────────────────────────────────────────────────────

struct ParsedMmapSegmentPaths {
	std::vector<std::string> data;
	std::vector<std::string> canid;
	std::vector<std::string> channel;
	std::vector<std::string> direction;
};

class ParsedMmapInterface {
public:
	explicit ParsedMmapInterface(std::string mmap_prefix);

	int32_t open_mmap();
	int32_t write_entries(const std::vector<LogRecord>& entries);
	void set_file_path(std::string file_path);
	std::string get_file_path() const;
	void close_mmap();

	// Would only return std::vector<ParsedEntry> if you have to allocate new objects.
	// Since your ParsedEntry already lives inside the mapped file:
	// seg_entries_[i]
	// There is no reason to copy them into a vector std::vector<LogRecord>.
	// std::span is great inside C++, but Python doesn't have a native concept of std::span.
	// Internally pybind11 converts the Python list into
	// std::vector<ParsedEntry>
	// before calling C++.
	std::vector<ParsedEntry> read_page(int64_t first, int64_t last) const;
	std::vector<ParsedEntry> read_page_from_can_id(uint32_t can_id,
	                                                          int64_t first,
	                                                          int64_t last);
	std::vector<ParsedEntry> read_all_entries() const;
	std::vector<ParsedEntry> read_page_from_can_ids(const std::vector<uint32_t>& can_ids,
	                                                           int64_t first,
	                                                           int64_t last);
	std::vector<ParsedEntry> read_page_from_can_id_changed(uint32_t can_id,
	                                                                  int64_t first,
	                                                                  int64_t last);
	std::vector<ParsedEntry> read_page_from_can_ids_changed(const std::vector<uint32_t>& can_ids,
	                                                                   int64_t first,
	                                                                   int64_t last);
	std::vector<ParsedEntry> read_page_from_channel(const std::string& channel,
	                                                           int64_t first,
	                                                           int64_t last);
	std::vector<ParsedEntry> read_page_from_channels(const std::vector<std::string>& channels,
	                                                            int64_t first,
	                                                            int64_t last);
	std::vector<ParsedEntry> read_page_from_direction(const std::string& direction,
	                                                             int64_t first,
	                                                             int64_t last);
	std::vector<ParsedEntry> read_page_from_directions(const std::vector<std::string>& directions,
	                                                              int64_t first,
	                                                              int64_t last);

	// Unified multi-factor query (SQLite filter -> mmap payload fetch).
	// Replaces the single-factor read_page_from_* methods above: combine any of
	// can_ids / channels / directions / changed / time-range in one LogQuery.
	// [first, last] is the inclusive page window into the FILTERED result.
	std::vector<ParsedEntry> read_page_multi(const LogQuery& query,
	                                         int64_t first,
	                                         int64_t last);

	int32_t get_first_last_timestamp(double& out_first_ts,
	                                 double& out_last_ts) const;

	uint64_t fetch_count() const;
	const std::string& token_path() const;
	int32_t last_error_code() const;

private:
	friend class ParsedMmapInterfaceTestAccessor;

	std::vector<std::string> data_segment_paths() const;
	std::vector<std::string> canid_segment_paths() const;
	std::vector<std::string> channel_segment_paths() const;
	std::vector<std::string> direction_segment_paths() const;
	ParsedMmapSegmentPaths all_segment_paths() const;

	bool is_segment_writers_ready() const;
	void reset_runtime_only();
	void clear_last_error() const;
	std::vector<ParsedEntry> read_rows_from_data(const std::vector<uint64_t>& rows) const;

	std::string mmap_prefix_;
	file_service::mmap::DataMmapInterface data_;
	file_service::mmap::CanIdIndexMmapInterface canid_;
	file_service::mmap::ChannelIndexMmapInterface channel_;
	file_service::mmap::DirectionIndexMmapInterface direction_;
	file_service::LogIndexDatabase index_db_;
	file_service::mmap::IndexBuckets buckets_;
	bool initialized_ = false;
	mutable int32_t last_error_code_ = 0;
};

} // namespace file_service
