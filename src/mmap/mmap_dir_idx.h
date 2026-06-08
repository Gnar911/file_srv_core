
#pragma once

#include <cstdint>
#include <cstddef>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "index_mmap_layout.h"
#include "mmap_data.h"
#include "mmap_wrapper.h"

namespace file_service {
namespace mmap {

class DirectionIndexMmapInterface {
public:
    explicit DirectionIndexMmapInterface(std::string index_base);

    void reset();
    bool is_ready() const;

    int32_t open_and_init();
    int32_t write_from_buckets(const IndexBuckets& buckets);
    void close_and_finalize();
    std::vector<uint32_t> get_page_from_direction_row_indices(const std::string& direction,
                                                               int64_t first_line,
                                                               int64_t page_size);
    std::vector<uint32_t> get_page_from_directions_row_indices(const std::vector<std::string>& directions,
                                                                int64_t first_line,
                                                                int64_t page_size);
    std::vector<std::string> segment_paths() const;
    uint32_t segment_count() const;

private:
    struct DirectionCatalogSegment {
        std::string seg_path;
        uint64_t row_pool_base = 0;
        uint64_t row_pool_off = 0;
        uint32_t count = 0;
        uint8_t direction_raw = 0;
    };

    struct MergeSourceSegment {
        std::string seg_path;
        uint64_t pool_base = 0;
        uint64_t pool_off = 0;
        uint32_t count = 0;
    };

    struct MergeHeapNode {
        uint32_t row_value = 0;
        size_t source_index = 0;
        size_t cursor = 0;
    };

    struct MergeHeapNodeGreater {
        bool operator()(const MergeHeapNode& lhs, const MergeHeapNode& rhs) const {
            return lhs.row_value > rhs.row_value;
        }
    };

    struct MultiDirectionMergeKey {
        std::vector<std::string> directions;

        bool operator==(const MultiDirectionMergeKey& other) const {
            return directions == other.directions;
        }
    };

    struct MultiDirectionMergeKeyHash {
        size_t operator()(const MultiDirectionMergeKey& key) const {
            size_t h = 0;
            for (const auto& direction : key.directions) {
                const size_t sh = std::hash<std::string>{}(direction);
                h ^= sh + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    struct MultiDirectionMergeState {
        std::vector<std::string> unique_directions;
        std::vector<std::vector<MergeSourceSegment>> source_segments;
        std::vector<size_t> source_totals;
        std::priority_queue<MergeHeapNode, std::vector<MergeHeapNode>, MergeHeapNodeGreater> heap;
        size_t next_first_line = 0;
    };

    std::string make_segment_family_path(const char* family_suffix,
                                         uint32_t seg_idx) const;
    std::string normalize_direction_key(const std::string& direction) const;
    bool open_segment(uint32_t index);
    void ensure_direction_catalog_loaded();
    std::vector<uint32_t> read_direction_row_page_internal(
        const std::string& direction,
        size_t first_line,
        size_t page_size);
    std::vector<uint32_t> merge_directions_page_internal(
        const std::vector<std::string>& directions,
        size_t first_line,
        size_t page_size);
    void clear_direction_catalog_cache();

    static constexpr uint32_t kDirectionIndexSegmentCapacity = 1'000'000;
    static constexpr uint32_t kDirectionIndexMaxDirections = 8;

    uint32_t seg_idx_ = 0;
    MMapHandle handle_ = {};
    DirectionIndexHeader* hdr_ = nullptr;
    DirectionFilter* table_ = nullptr;
    uint32_t* row_pool_ = nullptr;
    uint32_t tbl_idx_ = 0;
    uint32_t row_pool_off_ = 0;
    std::string index_base_;

    // Internal read-side cache for direction index metadata.
    std::unordered_map<std::string, std::vector<DirectionCatalogSegment>> direction_catalog_;
    std::unordered_map<MultiDirectionMergeKey, MultiDirectionMergeState, MultiDirectionMergeKeyHash> multi_direction_merge_state_;
    bool direction_catalog_ready_ = false;
};

} // namespace mmap
} // namespace file_service
