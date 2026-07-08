#include "mmap_canid_idx.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <utility>

#include "can_parser.h"

namespace file_service {
namespace mmap {

CanIdIndexMmapInterface::CanIdIndexMmapInterface(std::string index_base)
    : index_base_(std::move(index_base)) {}

std::string CanIdIndexMmapInterface::make_segment_family_path(const char* family_suffix,
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

void CanIdIndexMmapInterface::reset() {
    close_and_finalize();
    seg_idx_ = 0;
    clear_can_id_catalog_cache();
}

bool CanIdIndexMmapInterface::is_ready() const {
    return handle_.addr != nullptr
        && hdr_ != nullptr
        && filter_table_ != nullptr
        && row_pool_ != nullptr
        && changed_row_pool_ != nullptr
        && ts_pool_ != nullptr;
}

bool CanIdIndexMmapInterface::open_segment(uint32_t index) {
    const std::string idx_path = make_segment_family_path("", index);
    const size_t idx_size = sizeof(IndexHeader)
        + static_cast<size_t>(kIndexMaxCanIds) * sizeof(CANIDFilter)
        + static_cast<size_t>(kIndexSegmentCapacity) * sizeof(uint32_t)
        + static_cast<size_t>(kIndexSegmentCapacity) * sizeof(uint32_t)
        + static_cast<size_t>(kIndexSegmentCapacity) * sizeof(double);
    mmap_create_rw(idx_path.c_str(), idx_size, handle_);

    hdr_ = reinterpret_cast<IndexHeader*>(handle_.addr);
    hdr_->can_id_count = 0;
    hdr_->row_pool_size = 0;
    hdr_->changed_row_pool_size = 0;
    hdr_->ts_pool_size = 0;
    hdr_->max_can_ids = kIndexMaxCanIds;
    hdr_->max_row_pool_size = kIndexSegmentCapacity;
    hdr_->max_changed_row_pool_size = kIndexSegmentCapacity;
    hdr_->max_ts_pool_size = kIndexSegmentCapacity;
    hdr_->status = PARSER_STATUS_RUNNING;
    hdr_->segment_count = index + 1;

    filter_table_ = reinterpret_cast<CANIDFilter*>(
        reinterpret_cast<uint8_t*>(handle_.addr) + sizeof(IndexHeader));
    row_pool_ = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(handle_.addr)
        + sizeof(IndexHeader)
        + static_cast<size_t>(kIndexMaxCanIds) * sizeof(CANIDFilter));
    changed_row_pool_ = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(row_pool_) + static_cast<size_t>(kIndexSegmentCapacity) * sizeof(uint32_t));
    ts_pool_ = reinterpret_cast<double*>(
        reinterpret_cast<uint8_t*>(changed_row_pool_) + static_cast<size_t>(kIndexSegmentCapacity) * sizeof(uint32_t));

    filt_idx_ = 0;
    row_pool_off_ = 0;
    changed_row_pool_off_ = 0;
    ts_pool_off_ = 0;
    return true;
}

int32_t CanIdIndexMmapInterface::open_and_init() {
    reset();
    seg_idx_ = 0;
    if (!open_segment(seg_idx_)) {
        return -7;
    }
    return 0;
}

std::vector<uint32_t> CanIdIndexMmapInterface::get_page_from_can_id_row_indices(
    uint32_t can_id,
    int64_t first_line,
    int64_t page_size) {
    const size_t start = first_line > 0 ? static_cast<size_t>(first_line) : 0U;
    const size_t size = page_size > 0 ? static_cast<size_t>(page_size) : 0U;
    return read_row_page_internal(can_id, start, size);
}

std::vector<uint32_t> CanIdIndexMmapInterface::get_page_from_can_ids_row_indices(
    const std::vector<uint32_t>& can_ids,
    int64_t first_line,
    int64_t page_size) {
    const size_t start = first_line > 0 ? static_cast<size_t>(first_line) : 0U;
    const size_t size = page_size > 0 ? static_cast<size_t>(page_size) : 0U;
    return merge_can_ids_page_internal(can_ids, start, size, false);
}

std::vector<uint32_t> CanIdIndexMmapInterface::get_page_from_can_id_changed_row_indices(
    uint32_t can_id,
    int64_t first_line,
    int64_t page_size) {
    const size_t start = first_line > 0 ? static_cast<size_t>(first_line) : 0U;
    const size_t size = page_size > 0 ? static_cast<size_t>(page_size) : 0U;
    return read_changed_row_page_internal(can_id, start, size);
}

std::vector<uint32_t> CanIdIndexMmapInterface::get_page_from_can_ids_changed_row_indices(
    const std::vector<uint32_t>& can_ids,
    int64_t first_line,
    int64_t page_size) {
    const size_t start = first_line > 0 ? static_cast<size_t>(first_line) : 0U;
    const size_t size = page_size > 0 ? static_cast<size_t>(page_size) : 0U;
    return merge_can_ids_page_internal(can_ids, start, size, true);
}

int32_t CanIdIndexMmapInterface::write_from_buckets(const IndexBuckets& buckets) {
    if (!is_ready()) {
        return -18;
    }

    static const std::vector<uint32_t> kEmptyChangedRows;

    for (const auto& kv : buckets.can_id_rows) {
        const uint32_t can_id = kv.first;
        const auto& rows = kv.second;
        const auto ts_it = buckets.can_id_timestamps.find(can_id);
        if (ts_it == buckets.can_id_timestamps.end()) {
            return -10;
        }
        const auto& timestamps = ts_it->second;
        if (timestamps.size() != rows.size()) {
            return -11;
        }

        const auto changed_it = buckets.can_id_changed_rows.find(can_id);
        const auto& changed_rows = (changed_it != buckets.can_id_changed_rows.end())
            ? changed_it->second
            : kEmptyChangedRows;

        size_t pos = 0;
        size_t changed_pos = 0;
        while (pos < rows.size()) {
            if (row_pool_off_ >= kIndexSegmentCapacity
                || changed_row_pool_off_ >= kIndexSegmentCapacity
                || ts_pool_off_ >= kIndexSegmentCapacity
                || filt_idx_ >= kIndexMaxCanIds) {
                close_and_finalize();
                ++seg_idx_;
                if (!open_segment(seg_idx_)) {
                    return -8;
                }
            }

            const uint32_t row_avail = kIndexSegmentCapacity - row_pool_off_;
            const uint32_t changed_row_avail = kIndexSegmentCapacity - changed_row_pool_off_;
            const uint32_t ts_avail = kIndexSegmentCapacity - ts_pool_off_;
            const uint32_t avail_row_ts = (row_avail < ts_avail) ? row_avail : ts_avail;
            const uint32_t avail = (avail_row_ts < changed_row_avail) ? avail_row_ts : changed_row_avail;
            const uint32_t remaining = static_cast<uint32_t>(rows.size() - pos);
            const uint32_t take = (avail < remaining) ? avail : remaining;
            if (take == 0) {
                close_and_finalize();
                ++seg_idx_;
                if (!open_segment(seg_idx_)) {
                    return -9;
                }
                continue;
            }

            filter_table_[filt_idx_].can_id = can_id;
            filter_table_[filt_idx_].row_offset = row_pool_off_;
            filter_table_[filt_idx_].changed_row_offset = changed_row_pool_off_;
            filter_table_[filt_idx_].ts_offset = ts_pool_off_;
            filter_table_[filt_idx_].count = take;
            filter_table_[filt_idx_].changed_count = 0;

            for (uint32_t i = 0; i < take; ++i) {
                row_pool_[row_pool_off_ + i] = rows[pos + i];
                ts_pool_[ts_pool_off_ + i] = timestamps[pos + i];
            }

            size_t ch = changed_pos;
            for (uint32_t i = 0; i < take; ++i) {
                const uint32_t row_value = rows[pos + i];
                while (ch < changed_rows.size() && changed_rows[ch] < row_value) ++ch;
                if (ch < changed_rows.size() && changed_rows[ch] == row_value) {
                    changed_row_pool_[changed_row_pool_off_ + filter_table_[filt_idx_].changed_count] = row_value;
                    ++filter_table_[filt_idx_].changed_count;
                    ++ch;
                }
            }
            changed_pos = ch;

            row_pool_off_ += take;
            changed_row_pool_off_ += filter_table_[filt_idx_].changed_count;
            ts_pool_off_ += take;
            pos += take;
            ++filt_idx_;
            hdr_->segment_count = seg_idx_ + 1;
        }
    }

    hdr_->can_id_count = filt_idx_;
    hdr_->row_pool_size = row_pool_off_;
    hdr_->changed_row_pool_size = changed_row_pool_off_;
    hdr_->ts_pool_size = ts_pool_off_;

    return 0;
}

void CanIdIndexMmapInterface::clear_can_id_catalog_cache() {
    can_id_catalog_.clear();
    can_id_timestamp_bounds_.clear();
    multi_can_merge_state_.clear();
    can_id_catalog_ready_ = false;
}

void CanIdIndexMmapInterface::ensure_can_id_catalog_loaded() {
    if (can_id_catalog_ready_) {
        return;
    }

    can_id_catalog_.clear();
    can_id_timestamp_bounds_.clear();

    const uint32_t total_segments = segment_count();
    for (uint32_t seg_idx = 0; seg_idx < total_segments; ++seg_idx) {
        const std::string seg_path = make_segment_family_path("", seg_idx);

        MMapHandle seg_handle = {};
        mmap_open_ro(seg_path.c_str(), seg_handle);

        const auto* base = reinterpret_cast<const uint8_t*>(seg_handle.addr);
        const size_t bytes = seg_handle.size;
        if (base == nullptr || bytes < sizeof(IndexHeader)) {
            mmap_close(seg_handle);
            continue;
        }

        const auto* hdr = reinterpret_cast<const IndexHeader*>(base);
        const uint32_t can_id_count = std::min(hdr->can_id_count, hdr->max_can_ids);
        const uint64_t max_row_pool_size = static_cast<uint64_t>(hdr->max_row_pool_size);
        const uint64_t max_changed_pool_size = static_cast<uint64_t>(hdr->max_changed_row_pool_size);
        const uint64_t max_ts_pool_size = static_cast<uint64_t>(hdr->max_ts_pool_size);

        const uint64_t filter_base = sizeof(IndexHeader);
        const uint64_t row_pool_base = filter_base + static_cast<uint64_t>(hdr->max_can_ids) * sizeof(CANIDFilter);
        const uint64_t changed_pool_base = row_pool_base + max_row_pool_size * sizeof(uint32_t);
        const uint64_t ts_pool_base = changed_pool_base + max_changed_pool_size * sizeof(uint32_t);

        for (uint32_t i = 0; i < can_id_count; ++i) {
            const uint64_t filter_addr = filter_base + static_cast<uint64_t>(i) * sizeof(CANIDFilter);
            if (filter_addr + sizeof(CANIDFilter) > bytes) {
                break;
            }

            const auto* filter = reinterpret_cast<const CANIDFilter*>(base + filter_addr);
            if (filter->count == 0 && filter->changed_count == 0) {
                continue;
            }

            if (filter->row_offset + static_cast<uint64_t>(filter->count) > max_row_pool_size) {
                continue;
            }
            if (filter->changed_row_offset + static_cast<uint64_t>(filter->changed_count) > max_changed_pool_size) {
                continue;
            }
            if (filter->ts_offset + static_cast<uint64_t>(filter->count) > max_ts_pool_size) {
                continue;
            }

            CanIdCatalogSegment desc;
            desc.seg_path = seg_path;
            desc.row_pool_base = row_pool_base;
            desc.row_pool_off = filter->row_offset;
            desc.count = filter->count;
            desc.changed_pool_base = changed_pool_base;
            desc.changed_row_pool_off = filter->changed_row_offset;
            desc.changed_count = filter->changed_count;
            can_id_catalog_[filter->can_id].push_back(std::move(desc));

            if (filter->count > 0) {
                const uint64_t first_ts_addr = ts_pool_base + filter->ts_offset * sizeof(double);
                const uint64_t last_ts_addr = ts_pool_base + (filter->ts_offset + static_cast<uint64_t>(filter->count) - 1ULL) * sizeof(double);
                if (first_ts_addr + sizeof(double) <= bytes && last_ts_addr + sizeof(double) <= bytes) {
                    double first_ts = 0.0;
                    double last_ts = 0.0;
                    std::memcpy(&first_ts, base + first_ts_addr, sizeof(double));
                    std::memcpy(&last_ts, base + last_ts_addr, sizeof(double));

                    auto it = can_id_timestamp_bounds_.find(filter->can_id);
                    if (it == can_id_timestamp_bounds_.end()) {
                        can_id_timestamp_bounds_.emplace(filter->can_id, std::make_pair(first_ts, last_ts));
                    } else {
                        it->second.second = last_ts;
                    }
                }
            }
        }

        mmap_close(seg_handle);
    }

    can_id_catalog_ready_ = true;
}

std::vector<uint32_t> CanIdIndexMmapInterface::read_row_page_internal(
    uint32_t can_id,
    size_t first_line,
    size_t page_size) {
    ensure_can_id_catalog_loaded();

    auto it = can_id_catalog_.find(can_id);
    if (it == can_id_catalog_.end()) {
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
        mmap_open_ro(seg.seg_path.c_str(), seg_handle);

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

std::vector<uint32_t> CanIdIndexMmapInterface::read_changed_row_page_internal(
    uint32_t can_id,
    size_t first_line,
    size_t page_size) {
    ensure_can_id_catalog_loaded();

    auto it = can_id_catalog_.find(can_id);
    if (it == can_id_catalog_.end()) {
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

        const size_t seg_count = static_cast<size_t>(seg.changed_count);
        if (seg_count == 0) {
            continue;
        }

        const size_t skip_in_seg = (start > skipped) ? (start - skipped) : 0;
        if (skip_in_seg >= seg_count) {
            skipped += seg_count;
            continue;
        }

        const size_t read_start = skip_in_seg;
        const size_t read_count = std::min(remaining, seg_count - skip_in_seg);

        MMapHandle seg_handle = {};
        mmap_open_ro(seg.seg_path.c_str(), seg_handle);

        const auto* base = reinterpret_cast<const uint8_t*>(seg_handle.addr);
        const size_t bytes = seg_handle.size;
        const uint64_t addr = seg.changed_pool_base
            + (seg.changed_row_pool_off + static_cast<uint64_t>(read_start)) * sizeof(uint32_t);
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

std::vector<uint32_t> CanIdIndexMmapInterface::merge_can_ids_page_internal(
    const std::vector<uint32_t>& can_ids,
    size_t first_line,
    size_t page_size,
    bool changed) {
    ensure_can_id_catalog_loaded();

    const size_t start = first_line;
    const size_t size = page_size;
    if (size == 0) {
        return {};
    }

    std::vector<uint32_t> unique_can_ids;
    unique_can_ids.reserve(can_ids.size());
    std::unordered_set<uint32_t> seen;
    seen.reserve(can_ids.size());
    for (const uint32_t can_id : can_ids) {
        if (seen.insert(can_id).second) {
            unique_can_ids.push_back(can_id);
        }
    }

    MultiCanMergeKey state_key;
    state_key.changed = changed;
    state_key.can_ids = unique_can_ids;

    auto build_sources = [&](const std::vector<uint32_t>& ids,
                             std::vector<std::vector<MergeSourceSegment>>& source_segments,
                             std::vector<size_t>& source_totals) {
        source_segments.clear();
        source_totals.clear();
        source_segments.reserve(ids.size());
        source_totals.reserve(ids.size());

        for (const uint32_t can_id : ids) {
            std::vector<MergeSourceSegment> seg_list;
            size_t total = 0;

            const auto it = can_id_catalog_.find(can_id);
            if (it != can_id_catalog_.end()) {
                const auto& catalog = it->second;
                seg_list.reserve(catalog.size());

                for (const auto& seg : catalog) {
                    MergeSourceSegment src;
                    src.seg_path = seg.seg_path;
                    if (changed) {
                        if (seg.changed_count == 0) {
                            continue;
                        }
                        src.pool_base = seg.changed_pool_base;
                        src.pool_off = seg.changed_row_pool_off;
                        src.count = seg.changed_count;
                    } else {
                        if (seg.count == 0) {
                            continue;
                        }
                        src.pool_base = seg.row_pool_base;
                        src.pool_off = seg.row_pool_off;
                        src.count = seg.count;
                    }
                    total += static_cast<size_t>(src.count);
                    seg_list.push_back(std::move(src));
                }
            }

            source_totals.push_back(total);
            source_segments.push_back(std::move(seg_list));
        }
    };

    auto state_it = multi_can_merge_state_.find(state_key);
    if (state_it == multi_can_merge_state_.end() || state_it->second.next_first_line != start) {
        MultiCanMergeState fresh_state;
        fresh_state.unique_can_ids = unique_can_ids;
        build_sources(unique_can_ids, fresh_state.source_segments, fresh_state.source_totals);
        fresh_state.next_first_line = 0;
        multi_can_merge_state_[state_key] = std::move(fresh_state);
        state_it = multi_can_merge_state_.find(state_key);
    }

    MultiCanMergeState& state = state_it->second;

    std::unordered_map<std::string, MMapHandle> mmap_cache;

    auto open_mm = [&](const std::string& seg_path) -> const MMapHandle* {
        auto it = mmap_cache.find(seg_path);
        if (it != mmap_cache.end()) {
            return &it->second;
        }
        MMapHandle handle = {};
        mmap_open_ro(seg_path.c_str(), handle);
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

void CanIdIndexMmapInterface::close_and_finalize() {
    const bool had_open_segment = handle_.addr != nullptr && hdr_ != nullptr;
    if (had_open_segment) {
        hdr_->can_id_count = filt_idx_;
        hdr_->row_pool_size = row_pool_off_;
        hdr_->changed_row_pool_size = changed_row_pool_off_;
        hdr_->ts_pool_size = ts_pool_off_;
        hdr_->segment_count = seg_idx_ + 1;
        hdr_->status = PARSER_STATUS_DONE;
    }
    mmap_close(handle_);
    hdr_ = nullptr;
    filter_table_ = nullptr;
    row_pool_ = nullptr;
    changed_row_pool_ = nullptr;
    ts_pool_ = nullptr;
    filt_idx_ = 0;
    row_pool_off_ = 0;
    changed_row_pool_off_ = 0;
    ts_pool_off_ = 0;
    clear_can_id_catalog_cache();
}

std::vector<std::string> CanIdIndexMmapInterface::segment_paths() const {
    const uint32_t count = is_ready() ? (seg_idx_ + 1) : segment_count();

    std::vector<std::string> paths;
    paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        paths.push_back(make_segment_family_path("", i));
    }
    return paths;
}

uint32_t CanIdIndexMmapInterface::segment_count() const {
    if (is_ready()) {
        return seg_idx_ + 1;
    }

    const std::string header0_path = make_segment_family_path("", 0);
    MMapHandle header0 = {};
    mmap_open_ro(header0_path.c_str(), header0);
    if (header0.addr == nullptr || header0.size < sizeof(IndexHeader)) {
        mmap_close(header0);
        return 0;
    }

    const auto* hdr0 = reinterpret_cast<const IndexHeader*>(header0.addr);
    const uint32_t count = hdr0->segment_count;
    mmap_close(header0);
    return count;
}

} // namespace mmap
} // namespace file_service
