#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "can_decoder.h"
#include "mmap_wrapper.h"

namespace file_service {
namespace mmap {

class DecoderRowIndexChangedMmap {
public:
    explicit DecoderRowIndexChangedMmap(std::string base_path,
                                        uint32_t segment_capacity = 1'000'000U);

    int32_t open_and_init();
    int32_t write_at(uint64_t global_offset, uint32_t row_index);
    void publish_progress(uint64_t written_total, bool done);
    void close_and_finalize();

    std::vector<std::string> segment_paths() const;
    int32_t read_total_count(uint64_t& out_total_count) const;
    int32_t read_value(uint64_t global_offset, uint32_t& out_row_index) const;
    std::vector<uint32_t> read_page(int64_t first, int64_t last) const;

private:
    struct Segment {
        MMapHandle handle = {};
        SoAHeader* header = nullptr;
        uint32_t* values = nullptr;
    };

    int32_t open_segment_(uint32_t seg_idx);
    int32_t ensure_segment_(uint64_t global_offset);
    std::vector<std::string> planned_segment_paths_() const;

    std::string base_path_;
    uint32_t segment_capacity_ = 0;
    std::vector<Segment> segments_;
};

} // namespace mmap
} // namespace file_service
