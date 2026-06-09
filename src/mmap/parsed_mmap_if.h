#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mmap/mmap_canid_idx.h"
#include "mmap/mmap_chnl_idx.h"
#include "mmap/mmap_data.h"
#include "mmap/mmap_dir_idx.h"
#include "parsed_entry_layout.h"

namespace file_service {

struct ParsedMmapSegmentPaths {
	std::vector<std::string> data;
	std::vector<std::string> canid;
	std::vector<std::string> channel;
	std::vector<std::string> direction;
};

class ParsedMmapInterface {
public:
	explicit ParsedMmapInterface(std::string token_id);

	int32_t open_mmap();
	int32_t write_entries(const std::vector<ParsedEntry>& parsed_entries);
	void close_mmap();
	std::vector<ParsedEntry> read_page(int64_t first, int64_t last) const;
	std::vector<ParsedEntry> read_page_from_can_id(uint32_t can_id,
	                                                          int64_t first,
	                                                          int64_t last);
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

	uint64_t get_total_entries_num() const;
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

	std::string token_id_;
	file_service::mmap::DataMmapInterface data_;
	file_service::mmap::CanIdIndexMmapInterface canid_;
	file_service::mmap::ChannelIndexMmapInterface channel_;
	file_service::mmap::DirectionIndexMmapInterface direction_;
	file_service::mmap::IndexBuckets buckets_;
	bool initialized_ = false;
	mutable int32_t last_error_code_ = 0;
};

} // namespace file_service
