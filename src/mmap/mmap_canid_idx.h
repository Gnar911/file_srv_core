#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <string>

#include "index_mmap_layout.h"
#include "mmap_data.h"
#include "mmap_wrapper.h"

namespace file_service {
namespace mmap {

class CanIdIndexMmapInterface {
public:
    explicit CanIdIndexMmapInterface(std::string index_base);

    void reset();
    bool is_ready() const;

    int32_t open_and_init();
    int32_t write_from_buckets(const IndexBuckets& buckets);
    void close_and_finalize();
    std::vector<uint32_t> get_page_from_can_id_row_indices(uint32_t can_id,
                                                            int64_t first_line,
                                                            int64_t page_size);
    std::vector<uint32_t> get_page_from_can_ids_row_indices(const std::vector<uint32_t>& can_ids,
                                                             int64_t first_line,
                                                             int64_t page_size);
    std::vector<uint32_t> get_page_from_can_id_changed_row_indices(uint32_t can_id,
                                                                    int64_t first_line,
                                                                    int64_t page_size);
    std::vector<uint32_t> get_page_from_can_ids_changed_row_indices(const std::vector<uint32_t>& can_ids,
                                                                     int64_t first_line,
                                                                     int64_t page_size);
    std::vector<std::string> segment_paths() const;
    uint32_t segment_count() const;

private:
    struct CanIdCatalogSegment {
        std::string seg_path;
        uint64_t row_pool_base = 0;
        uint64_t row_pool_off = 0;
        uint32_t count = 0;
        uint64_t changed_pool_base = 0;
        uint64_t changed_row_pool_off = 0;
        uint32_t changed_count = 0;
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

    struct MultiCanMergeKey {
        bool changed = false;
        std::vector<uint32_t> can_ids;

        bool operator==(const MultiCanMergeKey& other) const {
            return changed == other.changed && can_ids == other.can_ids;
        }
    };

    struct MultiCanMergeKeyHash {
        size_t operator()(const MultiCanMergeKey& key) const {
            size_t h = key.changed ? static_cast<size_t>(0x9e3779b97f4a7c15ULL) : 0ULL;
            for (const uint32_t can_id : key.can_ids) {
                h ^= static_cast<size_t>(can_id) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    struct MultiCanMergeState {
        std::vector<uint32_t> unique_can_ids;
        std::vector<std::vector<MergeSourceSegment>> source_segments;
        std::vector<size_t> source_totals;
        std::priority_queue<MergeHeapNode, std::vector<MergeHeapNode>, MergeHeapNodeGreater> heap;
        size_t next_first_line = 0;
    };

    std::string make_segment_family_path(const char* family_suffix,
                                         uint32_t seg_idx) const;
    bool open_segment(uint32_t index);
    void ensure_can_id_catalog_loaded();
    std::vector<uint32_t> read_row_page_internal(
        uint32_t can_id,
        size_t first_line,
        size_t page_size);
    std::vector<uint32_t> read_changed_row_page_internal(
        uint32_t can_id,
        size_t first_line,
        size_t page_size);
    std::vector<uint32_t> merge_can_ids_page_internal(
        const std::vector<uint32_t>& can_ids,
        size_t first_line,
        size_t page_size,
        bool changed);
    void clear_can_id_catalog_cache();

    static constexpr uint32_t kIndexSegmentCapacity = 1'000'000;
    static constexpr uint32_t kIndexMaxCanIds = 4096;

    uint32_t seg_idx_ = 0;
    IndexHeader* hdr_ = nullptr;
    CANIDFilter* filter_table_ = nullptr;
    uint32_t* row_pool_ = nullptr;
    uint32_t* changed_row_pool_ = nullptr;
    double* ts_pool_ = nullptr;
    uint32_t filt_idx_ = 0;
    uint32_t row_pool_off_ = 0;
    uint32_t changed_row_pool_off_ = 0;
    uint32_t ts_pool_off_ = 0;
    MMapHandle handle_ = {};
    std::string index_base_;

    // Internal read-side cache for CAN-ID index metadata.
    std::unordered_map<uint32_t, std::vector<CanIdCatalogSegment>> can_id_catalog_;
    std::unordered_map<uint32_t, std::pair<double, double>> can_id_timestamp_bounds_;
    std::unordered_map<MultiCanMergeKey, MultiCanMergeState, MultiCanMergeKeyHash> multi_can_merge_state_;
    bool can_id_catalog_ready_ = false;
};

} // namespace mmap
} // namespace file_service
