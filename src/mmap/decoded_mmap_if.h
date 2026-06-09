#pragma once

/*
Index -> Signal

Signal Directory Mmap
 └─ (can_id, signal_id) → offset, count
 
# This is for each singal 
struct SignalDirectoryEntry {
    uint32_t can_id; -> partition
    uint16_t signal_id; -> partition
    uint64_t sample_offset;
    uint32_t sample_count;
};

Array Of Structure: AoS
Signal Sample Mmap
 └─ [ row_index, value, raw_value ] 
struct SignalSample {
    uint32_t row_index;   // index into raw log mmap
    double   value;
    int64_t  raw_value;
};

For ONE signal:
samples[sample_offset + 0] → { row_index = 12,    value = ..., raw = ... }
samples[sample_offset + 1] → { row_index = 98,    value = ..., raw = ... }
samples[sample_offset + 2] → { row_index = 102,   value = ..., raw = ... }
samples[sample_offset + 3] → { row_index = 2044,  value = ..., raw = ... }
samples[sample_offset + 4] → { row_index = 2045,  value = ..., raw = ... }
samples[sample_offset + 5] → { row_index = 91002, value = ..., raw = ... }


20260608: Read for a while to understand this: we need offset so that a rawvalue.mmap could hold array of rawvalue of 
every signals but because the signals number (sample_count) are different -> it needs the offset to know where is the
start of that signal rawvalue teritory.

Struct Of Array: SoA
SignalDirectoryEntry
 └─ (can_id, signal_id) →
      index_offset
      value_offset
      rawvalue_offset
      sample_count


row_index_changed.mmap  → [row_index changed]
row_index.mmap   	→ [ row_index ]
value.mmap       	→ [ value ]
rawvalue.mmap    	→ [ raw_value of signal A (k rows)][ raw_value of signal B (n rows)] -> offset maybe k or n


-> THE HARDEST thing of using the mmap for signal is we not knowing the number of a signals will appear so that we
can not allocate the region for those signal, (we can not treat all the signals will appear equally time) 
-> the temporary solution is to do 2 pass, for counting total first


Current SoA
Signal A samples
    contiguous
Signal B samples
    contiguous
Reader:
jump directly using:
offset
count
Very fast.

Contiguous SoA → need counting pass.
Paged append-only mmap → need indexing/page chains.

20260608
SQLite → let SQLite handle pages and indexes.
*/

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "can_decoder.h"
#include "mmap/mmap_decoder_rawvalue.h"
#include "mmap/mmap_decoder_row_index.h"
#include "mmap/mmap_decoder_row_index_changed.h"
#include "mmap/mmap_decoder_value.h"
#include "mmap/mmap_header_constract.h"

namespace file_service {

struct DecodedSample {
    uint32_t row_index = 0;
    double value = 0.0;
    int64_t rawvalue = 0;
};

using DataHeader = file_service::MmapHeaderConstract;

struct SoAHeader {
    uint64_t sample_count;
    uint32_t capacity;
    uint32_t status;
    uint8_t  padding[16];
};

// Data to write into mmap
struct SignalDirHeader {
    uint32_t entry_count;
    uint32_t status;
    uint8_t  padding[24];
};

// Data to write into mmap
struct SignalDirectoryEntry {
    uint32_t can_id;
    uint16_t signal_id;
    uint16_t padding; //mmap
    uint64_t index_offset;
    uint64_t value_offset;
    uint64_t rawvalue_offset;
    uint64_t changed_index_offset;
    uint32_t sample_count;
    uint32_t changed_sample_count;
    uint16_t signal_count;
    uint16_t padding2; //mmap
};


struct DecoderMmapSegmentPaths {
    std::vector<std::string> signal_dir;
    std::vector<std::string> row_index_changed;
    std::vector<std::string> row_index;
    std::vector<std::string> value;
    std::vector<std::string> rawvalue;
};

class DecodedMmapWriteContext {
public:
    static constexpr uint32_t kDefaultSampleSegmentCapacity = 1'000'000U;
    static constexpr uint32_t kDefaultDirSegmentCapacity = 1'000'000U;

    explicit DecodedMmapWriteContext(std::string token_path,
                            uint32_t sample_segment_capacity = kDefaultSampleSegmentCapacity,
                            uint32_t dir_segment_capacity = kDefaultDirSegmentCapacity);

    int32_t open_and_init();
    int32_t write_directory_entry(uint32_t dir_out_idx,
                                  const SignalDirectoryEntry& entry);
    void finalize_directory();
    int32_t write_sample(uint64_t global_offset,
                         uint32_t row_index,
                         double value,
                         int64_t rawvalue);
    int32_t write_changed_row_index(uint64_t global_offset, uint32_t row_index);
    void publish_progress(uint64_t written_total,
                          uint64_t changed_written_total,
                          bool done);
    void close();

private:
    struct DirSegment {
        MMapHandle handle = {};
        SignalDirHeader* header = nullptr;
        SignalDirectoryEntry* entries = nullptr;
        uint32_t written = 0;
    };

    int32_t open_dir_segment_(uint32_t seg_idx);
    int32_t ensure_dir_segment_(uint32_t dir_out_idx);

    std::string signal_dir_base_;
    uint32_t dir_segment_capacity_ = 0;
    std::vector<DirSegment> dir_segments_;

    mmap::DecoderRowIndexChangedMmap row_index_changed_writer_;
    mmap::DecoderRowIndexMmap row_index_writer_;
    mmap::DecoderValueMmap value_writer_;
    mmap::DecoderRawValueMmap rawvalue_writer_;
};

class DecodedMmapInterface {
public:
    DecodedMmapInterface(std::string signal_dir_base,
                         std::string row_index_changed_base,
                         std::string row_index_base,
                         std::string value_base,
                         std::string rawvalue_base);

    int32_t open_mmap();
    void close_mmap();

    std::vector<SignalDirectoryEntry> read_directory_page(int64_t first, int64_t last) const;
    bool find_directory_entry(uint32_t can_id,
                              uint16_t signal_id,
                              SignalDirectoryEntry& out_entry) const;

    std::vector<DecodedSample> read_signal_samples(uint32_t can_id,
                                                   uint16_t signal_id,
                                                   int64_t first,
                                                   int64_t last) const;
    std::vector<uint32_t> read_signal_changed_row_indices(uint32_t can_id,
                                                          uint16_t signal_id,
                                                          int64_t first,
                                                          int64_t last) const;

    uint64_t get_total_signal_entries_num() const;
    int32_t last_error_code() const;

private:
    static std::string make_segment_path(const std::string& base_path, uint32_t seg_idx);
    static std::vector<std::string> discover_segments(const std::string& base_path);
    static std::pair<size_t, size_t> to_page_window(int64_t first, int64_t last);

    int32_t load_directory_cache();
    int32_t read_sample_capacity(const std::vector<std::string>& paths, uint32_t& out_capacity) const;
    int32_t read_u32_at(const std::vector<std::string>& paths, uint64_t global_offset, uint32_t& out) const;
    int32_t read_f64_at(const std::vector<std::string>& paths, uint64_t global_offset, double& out) const;
    int32_t read_i64_at(const std::vector<std::string>& paths, uint64_t global_offset, int64_t& out) const;

    bool is_opened() const;
    void clear_last_error() const;
    void set_last_error(int32_t code) const;

    std::string signal_dir_base_;
    std::string row_index_changed_base_;
    std::string row_index_base_;
    std::string value_base_;
    std::string rawvalue_base_;

    DecoderMmapSegmentPaths paths_;
    uint32_t sample_segment_capacity_ = 0;

    std::vector<SignalDirectoryEntry> directory_entries_;
    std::unordered_map<uint64_t, size_t> directory_lookup_;
    bool opened_ = false;
    mutable int32_t last_error_code_ = 0;
};

} // namespace file_service
