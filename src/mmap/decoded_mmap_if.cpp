
#include "decoded_mmap_if.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "can_analyzer_log.h"
#include "mmap/mmap_wrapper.h"

namespace file_service {
namespace {

constexpr int32_t kDecoderMmapInvalidArgs = -3001;
constexpr int32_t kDecoderMmapNotOpened = -3002;
constexpr int32_t kDecoderMmapNoSegments = -3003;
constexpr int32_t kDecoderMmapOpenFailed = -3004;
constexpr int32_t kDecoderMmapCorrupted = -3005;
constexpr int32_t kDecoderMmapOutOfRange = -3006;

std::string decoded_base_path(const std::string& token_path, const char* suffix) {
    return token_path + suffix;
}

uint64_t make_signal_key(uint32_t can_id, uint16_t signal_id) {
    return (static_cast<uint64_t>(can_id) << 16) | static_cast<uint64_t>(signal_id);
}

template <typename T>
int32_t read_typed_at(const std::vector<std::string>& paths,
                     uint32_t segment_capacity,
                     uint64_t global_offset,
                     T& out_value) {
    if (paths.empty() || segment_capacity == 0) {
        return kDecoderMmapInvalidArgs;
    }

    const uint64_t seg_idx = global_offset / static_cast<uint64_t>(segment_capacity);
    const uint64_t local = global_offset % static_cast<uint64_t>(segment_capacity);
    if (seg_idx >= paths.size()) {
        return kDecoderMmapOutOfRange;
    }

    MMapHandle handle = {};
    mmap_open_ro(paths[static_cast<size_t>(seg_idx)].c_str(), handle);

    if (handle.addr == nullptr || handle.size < sizeof(SoAHeader)) {
        mmap_close(handle);
        return kDecoderMmapCorrupted;
    }

    const auto* hdr = reinterpret_cast<const SoAHeader*>(handle.addr);
    if (hdr->capacity == 0 || local >= hdr->capacity) {
        mmap_close(handle);
        return kDecoderMmapOutOfRange;
    }

    const size_t offset = sizeof(SoAHeader) + static_cast<size_t>(local) * sizeof(T);
    if (offset + sizeof(T) > handle.size) {
        mmap_close(handle);
        return kDecoderMmapCorrupted;
    }

    std::memcpy(&out_value, reinterpret_cast<const uint8_t*>(handle.addr) + offset, sizeof(T));
    mmap_close(handle);
    return 0;
}

} // namespace

DecodedMmapWriteContext::DecodedMmapWriteContext(std::string token_path,
                                                 uint32_t sample_segment_capacity,
                                                 uint32_t dir_segment_capacity)
    : signal_dir_base_(decoded_base_path(token_path, ".signal_dir.mmap")),
      dir_segment_capacity_(dir_segment_capacity),
      row_index_changed_writer_(decoded_base_path(token_path, ".row_index_changed.mmap"), sample_segment_capacity),
      row_index_writer_(decoded_base_path(token_path, ".row_index.mmap"), sample_segment_capacity),
      value_writer_(decoded_base_path(token_path, ".value.mmap"), sample_segment_capacity),
      rawvalue_writer_(decoded_base_path(std::move(token_path), ".rawvalue.mmap"), sample_segment_capacity) {}

int32_t DecodedMmapWriteContext::open_dir_segment_(uint32_t seg_idx) {
    DirSegment seg;
    std::string stem = signal_dir_base_;
    if (stem.size() >= 5 && stem.compare(stem.size() - 5, 5, ".mmap") == 0) {
        stem.resize(stem.size() - 5);
    }
    char num[16];
    std::snprintf(num, sizeof(num), ".%03u.mmap", seg_idx);
    const std::string path = stem + num;
    const size_t seg_size = sizeof(SignalDirHeader)
        + static_cast<size_t>(dir_segment_capacity_) * sizeof(SignalDirectoryEntry);
    mmap_create_rw(path.c_str(), seg_size, seg.handle);
    seg.header = reinterpret_cast<SignalDirHeader*>(seg.handle.addr);
    seg.entries = reinterpret_cast<SignalDirectoryEntry*>(
        reinterpret_cast<uint8_t*>(seg.handle.addr) + sizeof(SignalDirHeader));
    seg.header->entry_count = 0;
    dir_segments_.push_back(std::move(seg));
    return 0;
}

int32_t DecodedMmapWriteContext::ensure_dir_segment_(uint32_t dir_out_idx) {
    if (dir_segment_capacity_ == 0) {
        return -4;
    }
    const uint32_t required_seg_idx = dir_out_idx / dir_segment_capacity_;
    while (dir_segments_.size() <= required_seg_idx) {
        const int32_t rc = open_dir_segment_(static_cast<uint32_t>(dir_segments_.size()));
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

int32_t DecodedMmapWriteContext::open_and_init() {
    close();

    if (dir_segment_capacity_ == 0) {
        return -4;
    }

    if (open_dir_segment_(0) != 0) {
        close();
        return -4;
    }

    if (row_index_changed_writer_.open_and_init() != 0) {
        close();
        return -10;
    }
    if (row_index_writer_.open_and_init() != 0) {
        close();
        return -5;
    }
    if (value_writer_.open_and_init() != 0) {
        close();
        return -7;
    }
    if (rawvalue_writer_.open_and_init() != 0) {
        close();
        return -8;
    }

    return 0;
}

int32_t DecodedMmapWriteContext::write_directory_entry(uint32_t dir_out_idx,
                                                       const SignalDirectoryEntry& entry) {
    const int32_t ensure_rc = ensure_dir_segment_(dir_out_idx);
    if (ensure_rc != 0) {
        return ensure_rc;
    }
    const uint32_t seg_idx = dir_out_idx / dir_segment_capacity_;
    const uint32_t local = dir_out_idx % dir_segment_capacity_;
    DirSegment& seg = dir_segments_[seg_idx];
    seg.entries[local] = entry;
    seg.written++;
    return 0;
}

void DecodedMmapWriteContext::finalize_directory() {
    for (auto& seg : dir_segments_) {
        seg.header->entry_count = seg.written;
    }
}

int32_t DecodedMmapWriteContext::write_sample(uint64_t global_offset,
                                              uint32_t row_index,
                                              double value,
                                              int64_t rawvalue) {
    int32_t rc = row_index_writer_.write_at(global_offset, row_index);
    if (rc != 0) return rc;
    rc = value_writer_.write_at(global_offset, value);
    if (rc != 0) return rc;
    rc = rawvalue_writer_.write_at(global_offset, rawvalue);
    return rc;
}

int32_t DecodedMmapWriteContext::write_changed_row_index(uint64_t global_offset,
                                                         uint32_t row_index) {
    return row_index_changed_writer_.write_at(global_offset, row_index);
}

void DecodedMmapWriteContext::publish_progress(uint64_t written_total,
                                               uint64_t changed_written_total,
                                               bool done) {
    row_index_writer_.publish_progress(written_total, done);
    value_writer_.publish_progress(written_total, done);
    rawvalue_writer_.publish_progress(written_total, done);
    row_index_changed_writer_.publish_progress(changed_written_total, done);
}

void DecodedMmapWriteContext::close() {
    for (auto& seg : dir_segments_) {
        mmap_close(seg.handle);
        seg.header = nullptr;
        seg.entries = nullptr;
        seg.written = 0;
    }
    dir_segments_.clear();

    row_index_changed_writer_.close_and_finalize();
    row_index_writer_.close_and_finalize();
    value_writer_.close_and_finalize();
    rawvalue_writer_.close_and_finalize();
}

DecodedMmapInterface::DecodedMmapInterface(std::string signal_dir_base,
                                           std::string row_index_changed_base,
                                           std::string row_index_base,
                                           std::string value_base,
                                           std::string rawvalue_base)
    : signal_dir_base_(std::move(signal_dir_base)),
      row_index_changed_base_(std::move(row_index_changed_base)),
      row_index_base_(std::move(row_index_base)),
      value_base_(std::move(value_base)),
      rawvalue_base_(std::move(rawvalue_base)) {}

std::string DecodedMmapInterface::make_segment_path(const std::string& base_path, uint32_t seg_idx) {
    std::string stem = base_path;
    if (stem.size() >= 5 && stem.compare(stem.size() - 5, 5, ".mmap") == 0) {
        stem.resize(stem.size() - 5);
    }
    char num[16];
    std::snprintf(num, sizeof(num), ".%03u.mmap", seg_idx);
    return stem + num;
}

std::vector<std::string> DecodedMmapInterface::discover_segments(const std::string& base_path) {
    std::vector<std::string> out;
    out.reserve(64);
    for (uint32_t i = 0; i < 10000; ++i) {
        const std::string path = make_segment_path(base_path, i);
        if (!mmap_file_exists(path.c_str())) {
            if (i == 0) {
                break;
            }
            break;
        }
        out.push_back(path);
    }
    return out;
}

std::pair<size_t, size_t> DecodedMmapInterface::to_page_window(int64_t first, int64_t last) {
    const size_t start = (first > 0) ? static_cast<size_t>(first) : 0U;
    if (last < first) {
        return {start, 0U};
    }
    return {start, static_cast<size_t>((last - first) + 1)};
}

int32_t DecodedMmapInterface::open_mmap() {
    clear_last_error();
    close_mmap();

    paths_.signal_dir = discover_segments(signal_dir_base_);
    paths_.row_index_changed = discover_segments(row_index_changed_base_);
    paths_.row_index = discover_segments(row_index_base_);
    paths_.value = discover_segments(value_base_);
    paths_.rawvalue = discover_segments(rawvalue_base_);

    if (paths_.signal_dir.empty() || paths_.row_index_changed.empty() ||
        paths_.row_index.empty() || paths_.value.empty() || paths_.rawvalue.empty()) {
        set_last_error(kDecoderMmapNoSegments);
        return kDecoderMmapNoSegments;
    }

    uint32_t cap_ridx_changed = 0;
    uint32_t cap_ridx = 0;
    uint32_t cap_value = 0;
    uint32_t cap_raw = 0;

    int32_t rc = read_sample_capacity(paths_.row_index_changed, cap_ridx_changed);
    if (rc != 0) {
        set_last_error(rc);
        return rc;
    }

    rc = read_sample_capacity(paths_.row_index, cap_ridx);
    if (rc != 0) {
        set_last_error(rc);
        return rc;
    }

    rc = read_sample_capacity(paths_.value, cap_value);
    if (rc != 0) {
        set_last_error(rc);
        return rc;
    }

    rc = read_sample_capacity(paths_.rawvalue, cap_raw);
    if (rc != 0) {
        set_last_error(rc);
        return rc;
    }

    if (cap_ridx_changed == 0 || cap_ridx_changed != cap_ridx ||
        cap_ridx != cap_value || cap_value != cap_raw) {
        set_last_error(kDecoderMmapCorrupted);
        return kDecoderMmapCorrupted;
    }
    sample_segment_capacity_ = cap_ridx;

    rc = load_directory_cache();
    if (rc != 0) {
        set_last_error(rc);
        return rc;
    }

    opened_ = true;
    return 0;
}

void DecodedMmapInterface::close_mmap() {
    opened_ = false;
    sample_segment_capacity_ = 0;
    directory_entries_.clear();
    directory_lookup_.clear();
    paths_ = DecoderMmapSegmentPaths{};
}

int32_t DecodedMmapInterface::load_directory_cache() {
    directory_entries_.clear();
    directory_lookup_.clear();

    size_t global_idx = 0;
    for (const std::string& p : paths_.signal_dir) {
        MMapHandle handle = {};
        mmap_open_ro(p.c_str(), handle);

        if (handle.addr == nullptr || handle.size < sizeof(SignalDirHeader)) {
            mmap_close(handle);
            return kDecoderMmapCorrupted;
        }

        const auto* hdr = reinterpret_cast<const SignalDirHeader*>(handle.addr);
        const size_t max_entries = (handle.size - sizeof(SignalDirHeader)) / sizeof(SignalDirectoryEntry);
        const size_t entry_count = std::min(static_cast<size_t>(hdr->entry_count), max_entries);
        const auto* entries = reinterpret_cast<const SignalDirectoryEntry*>(
            reinterpret_cast<const uint8_t*>(handle.addr) + sizeof(SignalDirHeader));

        for (size_t i = 0; i < entry_count; ++i) {
            directory_entries_.push_back(entries[i]);
            const uint64_t key = make_signal_key(entries[i].can_id, entries[i].signal_id);
            if (directory_lookup_.find(key) == directory_lookup_.end()) {
                directory_lookup_[key] = global_idx;
            }
            ++global_idx;
        }

        mmap_close(handle);
    }

    return 0;
}

int32_t DecodedMmapInterface::read_sample_capacity(const std::vector<std::string>& paths,
                                                   uint32_t& out_capacity) const {
    out_capacity = 0;
    if (paths.empty()) {
        return kDecoderMmapNoSegments;
    }

    MMapHandle handle = {};
    mmap_open_ro(paths.front().c_str(), handle);

    if (handle.addr == nullptr || handle.size < sizeof(SoAHeader)) {
        mmap_close(handle);
        return kDecoderMmapCorrupted;
    }

    const auto* hdr = reinterpret_cast<const SoAHeader*>(handle.addr);
    out_capacity = hdr->capacity;
    mmap_close(handle);

    if (out_capacity == 0) {
        return kDecoderMmapCorrupted;
    }

    return 0;
}

int32_t DecodedMmapInterface::read_u32_at(const std::vector<std::string>& paths,
                                          uint64_t global_offset,
                                          uint32_t& out) const {
    return read_typed_at<uint32_t>(paths, sample_segment_capacity_, global_offset, out);
}

int32_t DecodedMmapInterface::read_f64_at(const std::vector<std::string>& paths,
                                          uint64_t global_offset,
                                          double& out) const {
    return read_typed_at<double>(paths, sample_segment_capacity_, global_offset, out);
}

int32_t DecodedMmapInterface::read_i64_at(const std::vector<std::string>& paths,
                                          uint64_t global_offset,
                                          int64_t& out) const {
    return read_typed_at<int64_t>(paths, sample_segment_capacity_, global_offset, out);
}

bool DecodedMmapInterface::is_opened() const {
    return opened_;
}

void DecodedMmapInterface::clear_last_error() const {
    last_error_code_ = 0;
}

void DecodedMmapInterface::set_last_error(int32_t code) const {
    last_error_code_ = code;
}

std::vector<SignalDirectoryEntry> DecodedMmapInterface::read_directory_page(int64_t first,
                                                                             int64_t last) const {
    clear_last_error();
    if (!is_opened()) {
        set_last_error(kDecoderMmapNotOpened);
        return {};
    }

    const auto [start, page_size] = to_page_window(first, last);
    if (page_size == 0 || start >= directory_entries_.size()) {
        return {};
    }

    const size_t count = std::min(page_size, directory_entries_.size() - start);
    std::vector<SignalDirectoryEntry> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(directory_entries_[start + i]);
    }
    return out;
}

bool DecodedMmapInterface::find_directory_entry(uint32_t can_id,
                                                uint16_t signal_id,
                                                SignalDirectoryEntry& out_entry) const {
    clear_last_error();
    if (!is_opened()) {
        set_last_error(kDecoderMmapNotOpened);
        return false;
    }

    const uint64_t key = make_signal_key(can_id, signal_id);
    const auto it = directory_lookup_.find(key);
    if (it == directory_lookup_.end() || it->second >= directory_entries_.size()) {
        set_last_error(kDecoderMmapOutOfRange);
        return false;
    }

    out_entry = directory_entries_[it->second];
    return true;
}

std::vector<DecodedSample> DecodedMmapInterface::read_signal_samples(uint32_t can_id,
                                                                     uint16_t signal_id,
                                                                     int64_t first,
                                                                     int64_t last) const {
    clear_last_error();

    SignalDirectoryEntry entry = {};
    if (!find_directory_entry(can_id, signal_id, entry)) {
        return {};
    }

    const auto [start, page_size] = to_page_window(first, last);
    if (page_size == 0 || start >= entry.sample_count) {
        return {};
    }

    const uint64_t available = static_cast<uint64_t>(entry.sample_count) - static_cast<uint64_t>(start);
    const uint64_t count = std::min<uint64_t>(page_size, available);

    std::vector<DecodedSample> out;
    out.reserve(static_cast<size_t>(count));

    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t pos = entry.index_offset + static_cast<uint64_t>(start) + i;
        DecodedSample sample;

        int32_t rc = read_u32_at(paths_.row_index, pos, sample.row_index);
        if (rc != 0) {
            set_last_error(rc);
            return {};
        }

        rc = read_f64_at(paths_.value, pos, sample.value);
        if (rc != 0) {
            set_last_error(rc);
            return {};
        }

        rc = read_i64_at(paths_.rawvalue, pos, sample.rawvalue);
        if (rc != 0) {
            set_last_error(rc);
            return {};
        }

        out.push_back(sample);
    }

    return out;
}

std::vector<uint32_t> DecodedMmapInterface::read_signal_changed_row_indices(uint32_t can_id,
                                                                             uint16_t signal_id,
                                                                             int64_t first,
                                                                             int64_t last) const {
    clear_last_error();

    SignalDirectoryEntry entry = {};
    if (!find_directory_entry(can_id, signal_id, entry)) {
        return {};
    }

    const auto [start, page_size] = to_page_window(first, last);
    if (page_size == 0 || start >= entry.changed_sample_count) {
        return {};
    }

    const uint64_t available = static_cast<uint64_t>(entry.changed_sample_count) - static_cast<uint64_t>(start);
    const uint64_t count = std::min<uint64_t>(page_size, available);

    std::vector<uint32_t> out;
    out.reserve(static_cast<size_t>(count));

    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t pos = entry.changed_index_offset + static_cast<uint64_t>(start) + i;
        uint32_t row = 0;
        const int32_t rc = read_u32_at(paths_.row_index_changed, pos, row);
        if (rc != 0) {
            set_last_error(rc);
            return {};
        }
        out.push_back(row);
    }

    return out;
}

uint64_t DecodedMmapInterface::get_total_signal_entries_num() const {
    clear_last_error();
    if (!is_opened()) {
        set_last_error(kDecoderMmapNotOpened);
        return 0;
    }
    return static_cast<uint64_t>(directory_entries_.size());
}

int32_t DecodedMmapInterface::last_error_code() const {
    return last_error_code_;
}

} // namespace file_service
