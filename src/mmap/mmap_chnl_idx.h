#pragma once

#include <cstddef>
#include <cstdint>
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

class ChannelIndexMmapInterface {
public:
    explicit ChannelIndexMmapInterface(std::string index_base);

    void reset();
    bool is_ready() const;

    int32_t open_and_init();
    int32_t write_from_buckets(const IndexBuckets& buckets);
    void close_and_finalize();
    std::vector<uint32_t> get_page_from_channel_row_indices(const std::string& channel,
                                                             int64_t first_line,
                                                             int64_t page_size);
    std::vector<uint32_t> get_page_from_channels_row_indices(const std::vector<std::string>& channels,
                                                              int64_t first_line,
                                                              int64_t page_size);
    std::vector<std::string> segment_paths() const;
    uint32_t segment_count() const;

private:
    struct ChannelCatalogSegment {
        std::string seg_path;
        uint64_t row_pool_base = 0;
        uint64_t row_pool_off = 0;
        uint32_t count = 0;
        uint8_t channel_index = 0;
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

    struct MultiChannelMergeKey {
        std::vector<std::string> channels;

        bool operator==(const MultiChannelMergeKey& other) const {
            return channels == other.channels;
        }
    };

    struct MultiChannelMergeKeyHash {
        size_t operator()(const MultiChannelMergeKey& key) const {
            size_t h = 0;
            for (const auto& channel : key.channels) {
                const size_t sh = std::hash<std::string>{}(channel);
                h ^= sh + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    struct MultiChannelMergeState {
        std::vector<std::string> unique_channels;
        std::vector<std::vector<MergeSourceSegment>> source_segments;
        std::vector<size_t> source_totals;
        std::priority_queue<MergeHeapNode, std::vector<MergeHeapNode>, MergeHeapNodeGreater> heap;
        size_t next_first_line = 0;
    };

    std::string make_segment_family_path(const char* family_suffix,
                                         uint32_t seg_idx) const;
    std::string normalize_channel_key(const std::string& channel) const;
    bool open_segment(uint32_t index);
    void ensure_channel_catalog_loaded();
    std::vector<uint32_t> read_channel_row_page_internal(
        const std::string& channel,
        size_t first_line,
        size_t page_size);
    std::vector<uint32_t> merge_channels_page_internal(
        const std::vector<std::string>& channels,
        size_t first_line,
        size_t page_size);
    void clear_channel_catalog_cache();

    static constexpr uint32_t kChannelIndexSegmentCapacity = 1'000'000;
    static constexpr uint32_t kChannelIndexMaxChannels = 64;

    uint32_t seg_idx_ = 0;
    ChannelIndexHeader* hdr_ = nullptr;
    ChannelFilter* table_ = nullptr;
    uint32_t* row_pool_ = nullptr;
    uint32_t tbl_idx_ = 0;
    uint32_t row_pool_off_ = 0;
    MMapHandle handle_ = {};
    std::string index_base_;

    // Internal read-side cache for channel index metadata.
    std::unordered_map<std::string, std::vector<ChannelCatalogSegment>> channel_catalog_;
    std::unordered_map<MultiChannelMergeKey, MultiChannelMergeState, MultiChannelMergeKeyHash> multi_channel_merge_state_;
    bool channel_catalog_ready_ = false;
};

} // namespace mmap
} // namespace file_service
