#include "mmap_dir_idx.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

#include "can_parser.h"

namespace file_service {
namespace mmap {

DirectionIndexMmapInterface::DirectionIndexMmapInterface(std::string index_base)
    : index_base_(std::move(index_base)) {}

std::string DirectionIndexMmapInterface::make_segment_family_path(const char* family_suffix,
                                                                  uint32_t seg_idx) const {
    std::string stem = index_base_;
    if (stem.size() >= 5 && stem.compare(stem.size() - 5, 5, ".mmap") == 0) {
        stem.resize(stem.size() - 5);
    }
    if (family_suffix && family_suffix[0] != '\0') {
        stem += family_suffix;
    }
    char num[16];
    std::snprintf(num, sizeof(num), ".%03u.mmap", seg_idx);
    return stem + num;
}

std::string DirectionIndexMmapInterface::normalize_direction_key(const std::string& direction) const {
    std::string key = direction;
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (key == "tx" || key == "1") {
        return "tx";
    }
    return "rx";
}

void DirectionIndexMmapInterface::reset() {
    close_and_finalize();
    seg_idx_ = 0;
    clear_direction_catalog_cache();
}

bool DirectionIndexMmapInterface::is_ready() const {
    return handle_.addr != nullptr
        && hdr_ != nullptr
        && table_ != nullptr
        && row_pool_ != nullptr;
}

bool DirectionIndexMmapInterface::open_segment(uint32_t index) {
    const std::string dir_path = make_segment_family_path(".direction", index);
    const size_t dir_size = sizeof(DirectionIndexHeader)
        + static_cast<size_t>(kDirectionIndexMaxDirections) * sizeof(DirectionFilter)
        + static_cast<size_t>(kDirectionIndexSegmentCapacity) * sizeof(uint32_t);
    if (!mmap_create_rw(dir_path.c_str(), dir_size, handle_)) {
        return false;
    }

    hdr_ = reinterpret_cast<DirectionIndexHeader*>(handle_.addr);
    hdr_->direction_count = 0;
    hdr_->row_pool_size = 0;
    hdr_->max_directions = kDirectionIndexMaxDirections;
    hdr_->max_row_pool_size = kDirectionIndexSegmentCapacity;
    hdr_->status = PARSER_STATUS_RUNNING;
    hdr_->segment_count = index + 1;

    table_ = reinterpret_cast<DirectionFilter*>(
        reinterpret_cast<uint8_t*>(handle_.addr) + sizeof(DirectionIndexHeader));
    row_pool_ = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(handle_.addr)
        + sizeof(DirectionIndexHeader)
        + static_cast<size_t>(kDirectionIndexMaxDirections) * sizeof(DirectionFilter));

    tbl_idx_ = 0;
    row_pool_off_ = 0;
    return true;
}

int32_t DirectionIndexMmapInterface::open_and_init() {
    reset();
    seg_idx_ = 0;
    if (!open_segment(seg_idx_)) {
        return -15;
    }
    return 0;
}

std::vector<uint32_t> DirectionIndexMmapInterface::get_page_from_direction_row_indices(
    const std::string& direction,
    int64_t first_line,
    int64_t page_size) {
    const size_t start = first_line > 0 ? static_cast<size_t>(first_line) : 0U;
    const size_t size = page_size > 0 ? static_cast<size_t>(page_size) : 0U;
    return read_direction_row_page_internal(direction, start, size);
}

std::vector<uint32_t> DirectionIndexMmapInterface::get_page_from_directions_row_indices(
    const std::vector<std::string>& directions,
    int64_t first_line,
    int64_t page_size) {
    const size_t start = first_line > 0 ? static_cast<size_t>(first_line) : 0U;
    const size_t size = page_size > 0 ? static_cast<size_t>(page_size) : 0U;
    return merge_directions_page_internal(directions, start, size);
}

int32_t DirectionIndexMmapInterface::write_from_buckets(const IndexBuckets& buckets) {
    if (!is_ready()) {
        return -18;
    }

    for (uint8_t direction = 0; direction < 2; ++direction) {
        const auto& rows = buckets.direction_rows[direction];
        if (rows.empty()) {
            continue;
        }

        size_t pos = 0;
        while (pos < rows.size()) {
            if (row_pool_off_ >= kDirectionIndexSegmentCapacity || tbl_idx_ >= kDirectionIndexMaxDirections) {
                close_and_finalize();
                ++seg_idx_;
                if (!open_segment(seg_idx_)) {
                    return -16;
                }
            }

            const uint32_t row_avail = kDirectionIndexSegmentCapacity - row_pool_off_;
            const uint32_t remaining = static_cast<uint32_t>(rows.size() - pos);
            const uint32_t take = (row_avail < remaining) ? row_avail : remaining;
            if (take == 0) {
                close_and_finalize();
                ++seg_idx_;
                if (!open_segment(seg_idx_)) {
                    return -17;
                }
                continue;
            }

            table_[tbl_idx_].direction = direction;
            std::memset(table_[tbl_idx_].padding0, 0, sizeof(table_[tbl_idx_].padding0));
            table_[tbl_idx_].row_offset = row_pool_off_;
            table_[tbl_idx_].count = take;
            table_[tbl_idx_].reserved = 0;

            for (uint32_t i = 0; i < take; ++i) {
                row_pool_[row_pool_off_ + i] = rows[pos + i];
            }

            row_pool_off_ += take;
            pos += take;
            ++tbl_idx_;
            hdr_->segment_count = seg_idx_ + 1;
        }
    }

    hdr_->direction_count = tbl_idx_;
    hdr_->row_pool_size = row_pool_off_;

    return 0;
}

void DirectionIndexMmapInterface::clear_direction_catalog_cache() {
    direction_catalog_.clear();
    multi_direction_merge_state_.clear();
    direction_catalog_ready_ = false;
}

void DirectionIndexMmapInterface::ensure_direction_catalog_loaded() {
    if (direction_catalog_ready_) {
        return;
    }

    direction_catalog_.clear();

    const uint32_t total_segments = segment_count();
    for (uint32_t seg_idx = 0; seg_idx < total_segments; ++seg_idx) {
        const std::string seg_path = make_segment_family_path(".direction", seg_idx);

        MMapHandle seg_handle = {};
        if (!mmap_open_ro(seg_path.c_str(), seg_handle)) {
            continue;
        }

        const auto* base = reinterpret_cast<const uint8_t*>(seg_handle.addr);
        const size_t bytes = seg_handle.size;
        if (base == nullptr || bytes < sizeof(DirectionIndexHeader)) {
            mmap_close(seg_handle);
            continue;
        }

        const auto* hdr = reinterpret_cast<const DirectionIndexHeader*>(base);
        const uint32_t direction_count = std::min(hdr->direction_count, hdr->max_directions);
        const uint64_t max_row_pool_size = static_cast<uint64_t>(hdr->max_row_pool_size);
        const uint64_t filter_base = sizeof(DirectionIndexHeader);
        const uint64_t row_pool_base = filter_base + static_cast<uint64_t>(hdr->max_directions) * sizeof(DirectionFilter);

        for (uint32_t i = 0; i < direction_count; ++i) {
            const uint64_t filter_addr = filter_base + static_cast<uint64_t>(i) * sizeof(DirectionFilter);
            if (filter_addr + sizeof(DirectionFilter) > bytes) {
                break;
            }

            const auto* filter = reinterpret_cast<const DirectionFilter*>(base + filter_addr);
            if (filter->count == 0) {
                continue;
            }
            if (filter->row_offset + static_cast<uint64_t>(filter->count) > max_row_pool_size) {
                continue;
            }

            const uint8_t direction_raw = filter->direction;
            const std::string direction_key = (direction_raw == 1) ? "tx" : "rx";

            DirectionCatalogSegment desc;
            desc.seg_path = seg_path;
            desc.row_pool_base = row_pool_base;
            desc.row_pool_off = filter->row_offset;
            desc.count = filter->count;
            desc.direction_raw = direction_raw;
            direction_catalog_[direction_key].push_back(std::move(desc));
        }

        mmap_close(seg_handle);
    }

    direction_catalog_ready_ = true;
}

std::vector<uint32_t> DirectionIndexMmapInterface::read_direction_row_page_internal(
    const std::string& direction,
    size_t first_line,
    size_t page_size) {
    ensure_direction_catalog_loaded();

    const std::string direction_key = normalize_direction_key(direction);
    auto it = direction_catalog_.find(direction_key);
    if (it == direction_catalog_.end()) {
        return {};
    }
    const auto& segs = it->second;

    const size_t start = first_line;
    const size_t size = page_size;
    if (size == 0) {
        return {};
    }

    std::vector<uint32_t> result;
    result.reserve(size);
    size_t skipped = 0;
    size_t remaining = size;

    for (const auto& seg : segs) {
        if (remaining == 0) {
            break;
        }

        const size_t seg_count = static_cast<size_t>(seg.count);
        const size_t skip_in_seg = (start > skipped) ? (start - skipped) : 0;
        if (skip_in_seg >= seg_count) {
            skipped += seg_count;
            continue;
        }

        const size_t read_start = skip_in_seg;
        const size_t read_count = std::min(remaining, seg_count - skip_in_seg);

        MMapHandle seg_handle = {};
        if (!mmap_open_ro(seg.seg_path.c_str(), seg_handle)) {
            skipped += seg_count;
            continue;
        }

        const auto* base = reinterpret_cast<const uint8_t*>(seg_handle.addr);
        const size_t bytes = seg_handle.size;
        const uint64_t addr = seg.row_pool_base
            + (seg.row_pool_off + static_cast<uint64_t>(read_start)) * sizeof(uint32_t);
        const uint64_t need = static_cast<uint64_t>(read_count) * sizeof(uint32_t);

        if (base != nullptr && addr + need <= bytes) {
            const auto* src = reinterpret_cast<const uint32_t*>(base + addr);
            result.insert(result.end(), src, src + read_count);
            remaining -= read_count;
        }

        mmap_close(seg_handle);
        skipped += seg_count;
    }

    return result;
}

std::vector<uint32_t> DirectionIndexMmapInterface::merge_directions_page_internal(
    const std::vector<std::string>& directions,
    size_t first_line,
    size_t page_size) {
    ensure_direction_catalog_loaded();

    const size_t start = first_line;
    const size_t size = page_size;
    if (size == 0) {
        return {};
    }

    std::vector<std::string> unique_directions;
    unique_directions.reserve(directions.size());
    std::unordered_set<std::string> seen;
    seen.reserve(directions.size());
    for (const auto& direction : directions) {
        const std::string key = normalize_direction_key(direction);
        if (seen.insert(key).second) {
            unique_directions.push_back(key);
        }
    }

    MultiDirectionMergeKey state_key;
    state_key.directions = unique_directions;

    auto build_sources = [&](const std::vector<std::string>& keys,
                             std::vector<std::vector<MergeSourceSegment>>& source_segments,
                             std::vector<size_t>& source_totals) {
        source_segments.clear();
        source_totals.clear();
        source_segments.reserve(keys.size());
        source_totals.reserve(keys.size());

        for (const auto& key : keys) {
            std::vector<MergeSourceSegment> seg_list;
            size_t total = 0;

            const auto it = direction_catalog_.find(key);
            if (it != direction_catalog_.end()) {
                const auto& catalog = it->second;
                seg_list.reserve(catalog.size());

                for (const auto& seg : catalog) {
                    if (seg.count == 0) {
                        continue;
                    }

                    MergeSourceSegment src;
                    src.seg_path = seg.seg_path;
                    src.pool_base = seg.row_pool_base;
                    src.pool_off = seg.row_pool_off;
                    src.count = seg.count;
                    total += static_cast<size_t>(src.count);
                    seg_list.push_back(std::move(src));
                }
            }

            source_totals.push_back(total);
            source_segments.push_back(std::move(seg_list));
        }
    };

    auto state_it = multi_direction_merge_state_.find(state_key);
    if (state_it == multi_direction_merge_state_.end() || state_it->second.next_first_line != start) {
        MultiDirectionMergeState fresh_state;
        fresh_state.unique_directions = unique_directions;
        build_sources(unique_directions, fresh_state.source_segments, fresh_state.source_totals);
        fresh_state.next_first_line = 0;
        multi_direction_merge_state_[state_key] = std::move(fresh_state);
        state_it = multi_direction_merge_state_.find(state_key);
    }

    MultiDirectionMergeState& state = state_it->second;

    std::unordered_map<std::string, MMapHandle> mmap_cache;

    auto open_mm = [&](const std::string& seg_path) -> const MMapHandle* {
        auto it = mmap_cache.find(seg_path);
        if (it != mmap_cache.end()) {
            return &it->second;
        }
        MMapHandle handle = {};
        if (!mmap_open_ro(seg_path.c_str(), handle)) {
            return nullptr;
        }
        auto inserted = mmap_cache.emplace(seg_path, handle);
        return &inserted.first->second;
    };

    auto read_at = [&](size_t source_index, size_t pos, uint32_t& out_row) -> bool {
        if (source_index >= state.source_segments.size()) {
            return false;
        }

        size_t offset = 0;
        for (const auto& seg : state.source_segments[source_index]) {
            const size_t seg_count = static_cast<size_t>(seg.count);
            if (pos < offset + seg_count) {
                const size_t local = pos - offset;
                const MMapHandle* handle = open_mm(seg.seg_path);
                if (handle == nullptr || handle->addr == nullptr) {
                    return false;
                }

                const uint64_t addr = seg.pool_base + (seg.pool_off + static_cast<uint64_t>(local)) * sizeof(uint32_t);
                if (addr + sizeof(uint32_t) > handle->size) {
                    return false;
                }

                std::memcpy(&out_row, reinterpret_cast<const uint8_t*>(handle->addr) + addr, sizeof(uint32_t));
                return true;
            }
            offset += seg_count;
        }
        return false;
    };

    auto push_source_head = [&](size_t source_index, size_t cursor) {
        uint32_t row_value = 0;
        if (!read_at(source_index, cursor, row_value)) {
            return;
        }
        MergeHeapNode node;
        node.row_value = row_value;
        node.source_index = source_index;
        node.cursor = cursor;
        state.heap.push(node);
    };

    auto pop_next = [&]() -> bool {
        if (state.heap.empty()) {
            return false;
        }

        const MergeHeapNode node = state.heap.top();
        state.heap.pop();

        const size_t source_index = node.source_index;
        const size_t next_cursor = node.cursor + 1;
        if (source_index < state.source_totals.size() && next_cursor < state.source_totals[source_index]) {
            push_source_head(source_index, next_cursor);
        }
        return true;
    };

    if (state.heap.empty() && state.next_first_line == 0) {
        for (size_t i = 0; i < state.source_totals.size(); ++i) {
            if (state.source_totals[i] > 0) {
                push_source_head(i, 0);
            }
        }
    }

    size_t current_offset = state.next_first_line;
    while (current_offset < start && !state.heap.empty()) {
        if (!pop_next()) {
            break;
        }
        ++current_offset;
    }

    std::vector<uint32_t> merged;
    merged.reserve(size);
    while (merged.size() < size && !state.heap.empty()) {
        const MergeHeapNode node = state.heap.top();
        merged.push_back(node.row_value);
        if (!pop_next()) {
            break;
        }
        ++current_offset;
    }

    state.next_first_line = current_offset;

    for (auto& kv : mmap_cache) {
        mmap_close(kv.second);
    }

    return merged;
}

void DirectionIndexMmapInterface::close_and_finalize() {
    const bool had_open_segment = handle_.addr != nullptr && hdr_ != nullptr;
    if (had_open_segment) {
        hdr_->direction_count = tbl_idx_;
        hdr_->row_pool_size = row_pool_off_;
        hdr_->segment_count = seg_idx_ + 1;
        hdr_->status = PARSER_STATUS_DONE;
    }
    mmap_close(handle_);
    hdr_ = nullptr;
    table_ = nullptr;
    row_pool_ = nullptr;
    tbl_idx_ = 0;
    row_pool_off_ = 0;
    clear_direction_catalog_cache();
}

std::vector<std::string> DirectionIndexMmapInterface::segment_paths() const {
    const uint32_t count = is_ready() ? (seg_idx_ + 1) : segment_count();

    std::vector<std::string> paths;
    paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        paths.push_back(make_segment_family_path(".direction", i));
    }
    return paths;
}

uint32_t DirectionIndexMmapInterface::segment_count() const {
    if (is_ready()) {
        return seg_idx_ + 1;
    }

    const std::string header0_path = make_segment_family_path(".direction", 0);
    MMapHandle header0 = {};
    if (!mmap_open_ro(header0_path.c_str(), header0)) {
        return 0;
    }
    if (header0.addr == nullptr || header0.size < sizeof(DirectionIndexHeader)) {
        mmap_close(header0);
        return 0;
    }

    const auto* hdr0 = reinterpret_cast<const DirectionIndexHeader*>(header0.addr);
    const uint32_t count = hdr0->segment_count;
    mmap_close(header0);
    return count;
}

} // namespace mmap
} // namespace file_service