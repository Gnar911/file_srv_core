#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mmap_header_constract.h"
#include "mmap_wrapper.h"
#include "parsed_entry_layout.h"

namespace file_service {
namespace mmap {

/*
20260706
SQLite stores the table:
row_index | timestamp | can_id | direction | channel | changed
If you create
CREATE INDEX idx_can_id ON log_index(can_id);
SQLite internally builds something similar to a B-tree.
Conceptually:
CAN ID
    ↓
row ids
You don't have to implement or maintain that data structure yourself.
When you do
SELECT row_index
FROM log_index
WHERE can_id = 0x123;
SQLite uses its index automatically.

unordered_map<uint32_t, vector<uint32_t>>
is basically a hash index.
SQLite's index is more like
B+ Tree
*/
// struct IndexBuckets {
//     std::unordered_map<uint32_t, std::vector<uint32_t>> can_id_rows;
//     std::unordered_map<uint32_t, std::vector<uint32_t>> can_id_changed_rows;
//     std::unordered_map<uint32_t, std::vector<double>> can_id_timestamps;
//     std::vector<std::string> channel_table;
//     std::vector<std::vector<uint32_t>> channel_rows;
//     std::vector<uint32_t> direction_rows[2];
// };

// struct AppendResult {
//     uint32_t row_index;
//     bool changed;
// };

class DataMmapInterface {
public:
    explicit DataMmapInterface(std::string base);

    std::vector<std::string> segment_paths() const;
    std::vector<ParsedEntry> get_page_from_row_indices(int64_t first_line,
                                                        int64_t page_size) const;

    void read_entry(uint64_t global_row,
                    ParsedEntry& out_entry) const;
    void read_entries(const std::vector<uint64_t>& rows,
                      std::vector<ParsedEntry>& out_entries) const;
    void read_all_entries(std::vector<ParsedEntry>& out_entries) const;
    void read_first_last_timestamp(double& out_first_ts,
                                   double& out_last_ts) const;
    uint64_t timestamp_lower_bound(uint64_t total_rows,
                                   double target_ts) const;
    uint64_t timestamp_upper_bound(uint64_t total_rows,
                                   double target_ts) const;
    void read_timestamp(uint64_t global_row, double& out_ts) const;
    void read_total_rows(uint64_t& out_total_rows) const;
    void read_source_file_path(std::string& out_path) const;
    void read_segment_write_count(const std::string& segment_path,
                                  uint64_t& out_count) const;
    void read_segment_capacity(const std::string& segment_path,
                               uint32_t& out_capacity) const;
    static std::string normalize_channel_key(const char* ch);

    void reset();
    bool is_ready() const;

    void open_and_init();
    void set_source_file_path(std::string file_path);
    void write_entries(const std::vector<LogRecord>& entries);
    void update_entry(uint32_t row_index, const LogRecord& record);
    //const IndexBuckets& index_buckets() const;
    //void update_entries(const std::vector<EntryUpdate>& entries);
    void close_and_finalize();
    uint32_t append_entry(const LogRecord& entry);

    uint64_t total_written() const;
    uint32_t segment_count() const;

private:
    struct LastTimestampTable {
        static constexpr uint32_t kSize = 0x2000;
        double last[kSize] = {0.0};
        uint8_t seen[kSize] = {0};

        inline double update_and_get_prev(uint32_t can_id, double ts) {
            if (can_id >= kSize) return ts;
            const double prev = seen[can_id] ? last[can_id] : ts;
            last[can_id] = ts;
            seen[can_id] = 1;
            return prev;
        }
    };

    struct PrevRaw {
        uint8_t len = 0;
        uint8_t data[64] = {0};
    };

    std::string make_segment_family_path(const char* family_suffix,
                                         uint32_t seg_idx) const;
    void read_header_metadata(uint32_t& out_segment_count,
                              uint32_t& out_capacity) const;
    void read_entry_from_segment(uint32_t seg_idx,
                                 uint64_t target_idx,
                                 ParsedEntry& out_entry) const;
    void append_segment_entries(uint32_t seg_idx,
                                std::vector<ParsedEntry>& out_entries) const;
    void open_segment(uint32_t index);

    static constexpr uint32_t kDataSegmentCapacity = 1'000'000;

    uint32_t seg_idx_ = 0;
    MMapHandle seg_handle_ = {};
    file_service::MmapHeaderConstract* seg_hdr_ = nullptr;
    LogRecord* seg_entries_ = nullptr;
    uint64_t seg_write_ = 0;
    uint32_t global_row_idx_ = 0;
    uint64_t total_written_ = 0;
    // IndexBuckets buckets_;
    LastTimestampTable last_timestamp_by_id_;
    //std::unordered_map<uint32_t, PrevRaw> last_raw_by_id_;
    std::unordered_map<std::string, uint8_t> channel_to_index_;
    std::string source_file_path_;
    std::string base_;
};

} // namespace mmap
} // namespace file_service