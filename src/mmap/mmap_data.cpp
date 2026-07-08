#include "mmap_data.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <utility>

#include "can_analyzer_log.h"
#include "can_parser.h"
#include "mmap_error_codes.h"
#include "mmap/mmap_wrapper.h"

namespace file_service {
namespace mmap {

DataMmapInterface::DataMmapInterface(std::string base)
    : base_(std::move(base)) {}

void DataMmapInterface::read_header_metadata(uint32_t& out_segment_count,
                                             uint32_t& out_capacity) const {
    out_segment_count = 0;
    out_capacity = 0;

    const std::string header0_path = make_segment_family_path("", 0);
    MMapHandle handle = {};
    mmap_open_ro(header0_path.c_str(), handle);

    if (handle.size < file_service::kMmapHeaderConstractSize) {
        // CBCM_ERROR("ensure_reader_state_loaded failed: invalid header map path='%s' size=%zu",
        //            header0_path.c_str(),
        //            handle.size);
        mmap_close(handle);
        throw MMapError(std::string("invalid header mmap: ") + header0_path + " size=" + std::to_string(handle.size));
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    out_segment_count = hdr->segment_count;
    out_capacity = hdr->capacity;
    mmap_close(handle);

    if (out_segment_count == 0 || out_capacity == 0) {
        // CBCM_ERROR("ensure_reader_state_loaded failed: zero metadata segment_count=%u capacity=%u path='%s'",
        //            out_segment_count,
        //            out_capacity,
        //            header0_path.c_str());
        throw MMapError(std::string("zero metadata header: ") + header0_path);
    }

    (void)file_service::mmap::error_code::kOk;
}

std::vector<ParsedEntry> DataMmapInterface::get_page_from_row_indices(int64_t first_line,
                                                                       int64_t page_size) const {
    const uint64_t start = first_line > 0 ? static_cast<uint64_t>(first_line) : 0ULL;
    const uint64_t size = page_size > 0 ? static_cast<uint64_t>(page_size) : 0ULL;

    if (size == 0) {
        return {};
    }

    std::vector<uint64_t> rows;
    rows.reserve(static_cast<size_t>(size));
    for (uint64_t i = 0; i < size; ++i) {
        rows.push_back(start + i);
    }

    std::vector<ParsedEntry> out_entries;
    (void)read_entries(rows, out_entries);
    return out_entries;
}

void DataMmapInterface::read_entry(uint64_t global_row,
                                   ParsedEntry& out_entry) const {
    uint32_t segment_count = 0;
    uint32_t mmap_capacity = 0;
    read_header_metadata(segment_count, mmap_capacity); // throws on error

    const uint64_t seg_idx = global_row / static_cast<uint64_t>(mmap_capacity);
    const uint64_t local_idx = global_row % static_cast<uint64_t>(mmap_capacity);
    if (seg_idx >= segment_count) {
        CBCM_ERROR("read_entry failed: seg_idx out of range global_row=%llu seg_idx=%llu segment_count=%u",
                   static_cast<unsigned long long>(global_row),
                   static_cast<unsigned long long>(seg_idx),
                   segment_count);
        throw MMapError(std::string("segment index out of range: ") + std::to_string(global_row));
    }

    read_entry_from_segment(static_cast<uint32_t>(seg_idx), local_idx, out_entry);
    out_entry.line_number = (global_row < static_cast<uint64_t>(UINT32_MAX))
        ? static_cast<uint32_t>(global_row + 1)
        : UINT32_MAX;
}

void DataMmapInterface::read_entry_from_segment(uint32_t seg_idx,
                                                uint64_t target_idx,
                                                ParsedEntry& out_entry) const {
    const std::string path = make_segment_family_path("", seg_idx);
    MMapHandle handle = {};
    mmap_open_ro(path.c_str(), handle);

    if (handle.size < file_service::kMmapHeaderConstractSize) {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment invalid header path='%s' size=%zu",
                   path.c_str(),
                   handle.size);
        mmap_close(handle);
        throw MMapError(std::string("invalid header mmap: ") + path + " size=" + std::to_string(handle.size));
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    if (hdr->capacity == 0 || target_idx >= hdr->capacity) {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment invalid index path='%s' target_idx=%llu capacity=%u",
                   path.c_str(),
                   static_cast<unsigned long long>(target_idx),
                   hdr->capacity);
        mmap_close(handle);
        throw MMapError(std::string("invalid read arg: ") + path);
    }

    const size_t offset = file_service::kMmapHeaderConstractSize
        + static_cast<size_t>(target_idx) * static_cast<size_t>(kLogRecordSize);
    const bool ok = offset + static_cast<size_t>(kLogRecordSize) <= handle.size;
    if (ok) {
        std::memset(&out_entry, 0, sizeof(out_entry));
        std::memcpy(&out_entry,
                    reinterpret_cast<const uint8_t*>(handle.addr) + offset,
                    static_cast<size_t>(kLogRecordSize));
        out_entry.last_timestamp = out_entry.timestamp;
        out_entry.changed = 0;
        out_entry.line_number = 0;
    } else {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment out-of-range path='%s' offset=%zu entry_size=%zu handle_size=%zu",
                   path.c_str(),
                   offset,
                   static_cast<size_t>(kLogRecordSize),
                   handle.size);
    }
    mmap_close(handle);
    if (!ok) {
        throw MMapError(std::string("read out of range: ") + path);
    }
}

void DataMmapInterface::read_entries(const std::vector<uint64_t>& rows,
                                     std::vector<ParsedEntry>& out_entries) const {
    out_entries.clear();
    if (rows.empty()) return;

    out_entries.reserve(rows.size());
    for (uint64_t row : rows) {
        ParsedEntry entry{};
        read_entry(row, entry); // throws on error
        out_entries.push_back(entry);
    }
}

void DataMmapInterface::read_all_entries(
    std::vector<ParsedEntry>& out_entries) const
{
    out_entries.clear();

    uint32_t segment_count;
    uint32_t mmap_capacity;

    read_header_metadata(segment_count, mmap_capacity); // throws on error

    for (uint32_t seg = 0; seg < segment_count; ++seg)
    {
        append_segment_entries(seg, out_entries); // may throw
    }
}

void DataMmapInterface::append_segment_entries(
    uint32_t seg_idx,
    std::vector<ParsedEntry>& out_entries) const
{
    const std::string path = make_segment_family_path("", seg_idx);

    MMapHandle handle{};
    mmap_open_ro(path.c_str(), handle);

    auto* hdr =
        reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);

    auto* entries =
        reinterpret_cast<const LogRecord*>(
            reinterpret_cast<const uint8_t*>(handle.addr)
            + file_service::kMmapHeaderConstractSize);

    for (uint64_t i = 0; i < hdr->write_count; ++i) {
        ParsedEntry out{};
        static_cast<LogRecord&>(out) = entries[i];
        out.last_timestamp = out.timestamp;
        out.changed = 0;
        const uint64_t row_1based = static_cast<uint64_t>(out_entries.size()) + 1ULL;
        out.line_number = (row_1based <= static_cast<uint64_t>(UINT32_MAX))
            ? static_cast<uint32_t>(row_1based)
            : UINT32_MAX;
        out_entries.push_back(out);
    }

    mmap_close(handle);

    (void)file_service::mmap::error_code::kOk;
}

void DataMmapInterface::read_first_last_timestamp(double& out_first_ts,
                                                  double& out_last_ts) const {
    out_first_ts = 0.0;
    out_last_ts = 0.0;

    uint64_t total_rows = 0;
    read_total_rows(total_rows); // throws on error
    if (total_rows == 0) {
        throw MMapError(std::string("no data for timestamps"));
    }

    read_timestamp(0, out_first_ts); // throws on error
    read_timestamp(total_rows - 1, out_last_ts); // throws on error
}

uint64_t DataMmapInterface::timestamp_lower_bound(uint64_t total_rows,
                                                double target_ts) const {
    uint64_t lo = 0;
    uint64_t hi = total_rows;
    while (lo < hi) {
        const uint64_t mid = (lo + hi) / 2;
        double ts = 0.0;
        bool ok = true;
        read_timestamp(mid, ts);
        if (!ok || ts < target_ts) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

uint64_t DataMmapInterface::timestamp_upper_bound(uint64_t total_rows,
                                                double target_ts) const {
    uint64_t lo = 0;
    uint64_t hi = total_rows;
    while (lo < hi) {
        const uint64_t mid = (lo + hi) / 2;
        double ts = 0.0;
        bool ok = true;
        read_timestamp(mid, ts);
        if (!ok || ts <= target_ts) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void DataMmapInterface::read_timestamp(uint64_t global_row,
                                      double& out_ts) const {
    ParsedEntry entry{};
    read_entry(global_row, entry); // throws on error
    out_ts = entry.timestamp;
}

void DataMmapInterface::read_total_rows(uint64_t& out_total_rows) const {
    out_total_rows = 0;
    uint32_t segment_count = 0;
    uint32_t mmap_capacity = 0;
    read_header_metadata(segment_count, mmap_capacity); // throws on error

    uint64_t total = 0;
    for (uint32_t seg_idx = 0; seg_idx < segment_count; ++seg_idx) {
        uint64_t count = 0;
        read_segment_write_count(make_segment_family_path("", seg_idx), count); // throws on error
        total += count;
    }
    out_total_rows = total;
}

void DataMmapInterface::read_source_file_path(std::string& out_path) const {
    out_path.clear();

    const std::string header0_path = make_segment_family_path("", 0);
    MMapHandle handle = {};
    mmap_open_ro(header0_path.c_str(), handle);

    if (handle.size < file_service::kMmapHeaderConstractSize) {
        mmap_close(handle);
        throw MMapError(std::string("invalid header mmap: ") + header0_path + " size=" + std::to_string(handle.size));
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    const size_t raw_len = static_cast<size_t>(hdr->source_file_path_len);
    const size_t max_len = sizeof(hdr->source_file_path);
    const size_t bounded_len = (raw_len <= max_len) ? raw_len : max_len;
    const size_t safe_len = strnlen(hdr->source_file_path, bounded_len);
    out_path.assign(hdr->source_file_path, safe_len);

    mmap_close(handle);
}

void DataMmapInterface::read_segment_write_count(const std::string& segment_path,
                                                 uint64_t& out_count) const {
    out_count = 0;
    if (segment_path.empty()) {
        throw MMapError(std::string("empty segment path"));
    }

    MMapHandle handle = {};
    mmap_open_ro(segment_path.c_str(), handle);
    if (handle.size < file_service::kMmapHeaderConstractSize) {
        mmap_close(handle);
        throw MMapError(std::string("invalid header mmap: ") + segment_path + " size=" + std::to_string(handle.size));
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    out_count = hdr->write_count;
    mmap_close(handle);
}

void DataMmapInterface::read_segment_capacity(const std::string& segment_path,
                                              uint32_t& out_capacity) const {
    out_capacity = 0;
    if (segment_path.empty()) {
        throw MMapError(std::string("empty capacity path"));
    }

    MMapHandle handle = {};
    mmap_open_ro(segment_path.c_str(), handle);
    if (handle.size < file_service::kMmapHeaderConstractSize) {
        mmap_close(handle);
        throw MMapError(std::string("invalid header mmap: ") + segment_path + " size=" + std::to_string(handle.size));
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    out_capacity = hdr->capacity;
    mmap_close(handle);
}

std::string DataMmapInterface::normalize_channel_key(const char* ch) {
    if (!ch || ch[0] == '\0') return "unknown";
    std::string s(ch);
    while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') s.pop_back();
    size_t start = 0;
    while (start < s.size() && static_cast<unsigned char>(s[start]) <= ' ') ++start;
    if (start > 0) s.erase(0, start);
    if (s.empty()) return "unknown";
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string DataMmapInterface::make_segment_family_path(const char* family_suffix,
                                                        uint32_t seg_idx) const {
    std::string stem = base_;
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

void DataMmapInterface::reset() {
    close_and_finalize();
    seg_idx_ = 0;
    global_row_idx_ = 0;
    total_written_ = 0;
    //buckets_ = IndexBuckets{};
    last_timestamp_by_id_ = LastTimestampTable{};
    channel_to_index_.clear();
}

// const IndexBuckets& DataMmapInterface::index_buckets() const {
//     return buckets_;
// }

void DataMmapInterface::set_source_file_path(std::string file_path) {
    source_file_path_ = std::move(file_path);
}

bool DataMmapInterface::is_ready() const {
    return seg_handle_.addr != nullptr
        && seg_hdr_ != nullptr
        && seg_entries_ != nullptr;
}

void DataMmapInterface::open_segment(uint32_t index) {
    CBCM_TRACE("DataMmapInterface::open_segment index=%u", index);
    const std::string seg_path = make_segment_family_path("", index);
    const size_t seg_size = file_service::kMmapHeaderConstractSize
        + static_cast<size_t>(kDataSegmentCapacity) * sizeof(LogRecord);
    mmap_create_rw(seg_path.c_str(), seg_size, seg_handle_);
    seg_hdr_ = reinterpret_cast<file_service::MmapHeaderConstract*>(seg_handle_.addr);
    seg_entries_ = reinterpret_cast<LogRecord*>(
        reinterpret_cast<uint8_t*>(seg_handle_.addr) + file_service::kMmapHeaderConstractSize);
    file_service::init_mmap_header_constract(*seg_hdr_, kDataSegmentCapacity, PARSER_STATUS_RUNNING, index + 1);
    file_service::set_mmap_header_source_file_path(*seg_hdr_, source_file_path_.c_str());
    seg_write_ = 0;
}

void DataMmapInterface::open_and_init() {
    reset();
    seg_idx_ = 0;
    open_segment(seg_idx_);
}


// Using the changed detection at the SQL Lite index storage
// bool changed = false;

// const uint8_t len =
//     std::min(entry.data_len,
//              static_cast<uint8_t>(sizeof(PrevRaw::data)));

// auto it = last_raw_by_id_.find(entry.can_id);

// if (it == last_raw_by_id_.end()) {
//     PrevRaw prev{};
//     prev.len = len;

//     if (len > 0) {
//         std::memcpy(prev.data, entry.data, len);
//     }

//     last_raw_by_id_.emplace(entry.can_id, prev);
// }
// else {
//     const PrevRaw& prev = it->second;

//     changed =
//         prev.len != len ||
//         (len > 0 &&
//          std::memcmp(prev.data, entry.data, len) != 0);

//     it->second.len = len;

//     if (len > 0) {
//         std::memcpy(it->second.data, entry.data, len);
//     }
// }
uint32_t DataMmapInterface::append_entry(const LogRecord& entry)
{
    if (!is_ready()) {
        throw MMapError("writer not ready");
    }

    if (seg_write_ >= kDataSegmentCapacity) {
        close_and_finalize();
        ++seg_idx_;
        open_segment(seg_idx_);
    }

    seg_entries_[seg_write_] = entry;

    const uint32_t row_index = global_row_idx_;

    ++global_row_idx_;
    ++seg_write_;
    ++total_written_;

    seg_hdr_->write_count = seg_write_;

    (void)last_timestamp_by_id_.update_and_get_prev(
        entry.can_id,
        entry.timestamp);

    return row_index;
}

void DataMmapInterface::write_entries(const std::vector<LogRecord>& entries) {
    if (!is_ready()) {
        throw MMapError(std::string("writer not ready"));
    }

    for (const LogRecord& entry : entries) {
        append_entry(entry);
        // if (seg_write_ >= kDataSegmentCapacity) {
        //     close_and_finalize();
        //     ++seg_idx_;
        //     open_segment(seg_idx_);
        // }
        // uint8_t changed = 0;
        // const uint8_t len = (entry.data_len <= sizeof(PrevRaw::data))
        //     ? entry.data_len
        //     : static_cast<uint8_t>(sizeof(PrevRaw::data));

        // auto it = last_raw_by_id_.find(entry.can_id);
        // if (it == last_raw_by_id_.end()) {
        //     PrevRaw prev;
        //     prev.len = len;
        //     if (len > 0) {
        //         std::memcpy(prev.data, entry.data, len);
        //     }
        //     last_raw_by_id_.emplace(entry.can_id, prev);
        // } else {
        //     const PrevRaw& prev = it->second;
        //     const bool is_changed = (prev.len != len)
        //         || (len > 0 && ::memcmp(prev.data, entry.data, len) != 0);
        //     changed = is_changed ? 1 : 0;
        //     it->second.len = len;
        //     if (len > 0) {
        //         std::memcpy(it->second.data, entry.data, len);
        //     }
        // }
        // seg_entries_[seg_write_] = entry;
        // const uint32_t row_idx = global_row_idx_++;

    //     bool changed = result.changed;
    //     int row_idx = result.row_index;

    //     buckets_.can_id_rows[entry.can_id].push_back(row_idx);
    //     if (changed == 1) {
    //         buckets_.can_id_changed_rows[entry.can_id].push_back(row_idx);
    //     }
    //     buckets_.can_id_timestamps[entry.can_id].push_back(entry.timestamp);

    //     (void)last_timestamp_by_id_.update_and_get_prev(entry.can_id, entry.timestamp);

    //     const std::string channel_key = normalize_channel_key(entry.channel);
    //     auto ch_it = channel_to_index_.find(channel_key);
    //     uint8_t channel_idx = 0;
    //     if (ch_it == channel_to_index_.end()) {
    //         channel_idx = static_cast<uint8_t>(buckets_.channel_table.size());
    //         channel_to_index_.emplace(channel_key, channel_idx);
    //         buckets_.channel_table.push_back(channel_key);
    //         buckets_.channel_rows.emplace_back();
    //     } else {
    //         channel_idx = ch_it->second;
    //     }
    //     if (channel_idx < buckets_.channel_rows.size()) {
    //         buckets_.channel_rows[channel_idx].push_back(row_idx);
    //     }
    //     buckets_.direction_rows[(entry.direction == 0) ? 0 : 1].push_back(row_idx);

    //     ++seg_write_;
    //     seg_hdr_->write_count = seg_write_;
    //     ++total_written_;
    // }
    }

    (void)file_service::mmap::error_code::kOk;
}

void DataMmapInterface::update_entry(uint32_t row_index, const LogRecord& record) {
    uint32_t segment_count = 0;
    uint32_t mmap_capacity = 0;
    read_header_metadata(segment_count, mmap_capacity);

    const uint64_t global_row = static_cast<uint64_t>(row_index);
    const uint64_t seg_idx = global_row / static_cast<uint64_t>(mmap_capacity);
    const uint64_t local_idx = global_row % static_cast<uint64_t>(mmap_capacity);
    if (seg_idx >= segment_count) {
        throw MMapError(std::string("segment index out of range"));
    }

    LogRecord updated{};
    updated.timestamp = record.timestamp;
    updated.can_id = record.can_id;
    updated.direction = record.direction;
    updated.data_len = record.data_len;
    const uint8_t copy_len = (updated.data_len <= sizeof(updated.data))
        ? updated.data_len
        : static_cast<uint8_t>(sizeof(updated.data));
    if (copy_len > 0) {
        std::memcpy(updated.data, record.data, copy_len);
    }
    const std::string channel = normalize_channel_key(record.channel);
    std::snprintf(updated.channel, sizeof(updated.channel), "%s", channel.c_str());

    const std::string path = make_segment_family_path("", static_cast<uint32_t>(seg_idx));
    MMapHandle handle = {};
    mmap_open_rw(path.c_str(), handle);

    if (handle.size < file_service::kMmapHeaderConstractSize) {
        mmap_close(handle);
        throw MMapError(std::string("invalid header mmap: ") + path + " size=" + std::to_string(handle.size));
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    if (local_idx >= hdr->write_count) {
        mmap_close(handle);
        throw MMapError(std::string("read out of range"));
    }

    auto* row_ptr = reinterpret_cast<LogRecord*>(
        reinterpret_cast<uint8_t*>(handle.addr) + file_service::kMmapHeaderConstractSize)
        + local_idx;
    *row_ptr = updated;
    mmap_close(handle);
}

// void DataMmapInterface::update_entries(const std::vector<EntryUpdate>& entries) {
//     if (entries.empty()) {
//         return;
//     }

//     uint32_t segment_count = 0;
//     uint32_t mmap_capacity = 0;
//     read_header_metadata(segment_count, mmap_capacity);

//     std::vector<EntryUpdate> ordered = entries;
//     std::stable_sort(ordered.begin(), ordered.end(), [](const EntryUpdate& a, const EntryUpdate& b) {
//         return a.row_index < b.row_index;
//     });

//     for (const EntryUpdate& u : ordered) {
//         // row_index is 0-based and addresses mmap rows directly.
//         const uint64_t global_row = static_cast<uint64_t>(u.row_index);
//         const uint64_t seg_idx = global_row / static_cast<uint64_t>(mmap_capacity);
//         const uint64_t local_idx = global_row % static_cast<uint64_t>(mmap_capacity);
//         if (seg_idx >= segment_count) {
//             throw MMapError(std::string("segment index out of range"));
//         }

//         LogRecord updated{};
//         updated.timestamp = u.record.timestamp;
//         updated.can_id = u.record.can_id;
//         updated.direction = u.record.direction;
//         updated.data_len = u.record.data_len;
//         const uint8_t copy_len = (updated.data_len <= sizeof(updated.data))
//             ? updated.data_len
//             : static_cast<uint8_t>(sizeof(updated.data));
//         if (copy_len > 0) {
//             std::memcpy(updated.data, u.record.data, copy_len);
//         }
//         const std::string channel = normalize_channel_key(u.record.channel);
//         std::snprintf(updated.channel, sizeof(updated.channel), "%s", channel.c_str());

//         const std::string path = make_segment_family_path("", static_cast<uint32_t>(seg_idx));
//         MMapHandle handle = {};
//         mmap_open_rw(path.c_str(), handle);

//         if (handle.size < file_service::kMmapHeaderConstractSize) {
//             mmap_close(handle);
//             throw MMapError(std::string("invalid header mmap: ") + path + " size=" + std::to_string(handle.size));
//         }

//         const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
//         if (local_idx >= hdr->write_count) {
//             mmap_close(handle);
//             throw MMapError(std::string("read out of range"));
//         }

//         auto* row_ptr = reinterpret_cast<LogRecord*>(
//             reinterpret_cast<uint8_t*>(handle.addr) + file_service::kMmapHeaderConstractSize)
//             + local_idx;

//         *row_ptr = updated;
//         mmap_close(handle);
//     }

//     (void)file_service::mmap::error_code::kOk;
// }

void DataMmapInterface::close_and_finalize() {
    CBCM_TRACE("DataMmapInterface::close_and_finalize seg_idx=%u seg_write=%llu",
              seg_idx_,
              static_cast<unsigned long long>(seg_write_));
    const bool had_open_segment = seg_handle_.addr != nullptr && seg_hdr_ != nullptr;
    if (had_open_segment) {
        seg_hdr_->write_count = seg_write_;
        seg_hdr_->status = PARSER_STATUS_DONE;
        seg_hdr_->segment_count = seg_idx_ + 1;
    }
    mmap_close(seg_handle_);
    seg_hdr_ = nullptr;
    seg_entries_ = nullptr;
    seg_write_ = 0;
}

uint64_t DataMmapInterface::total_written() const {
    return total_written_;
}

std::vector<std::string> DataMmapInterface::segment_paths() const {
    const uint32_t count = is_ready() ? (seg_idx_ + 1) : segment_count();

    std::vector<std::string> paths;
    paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        paths.push_back(make_segment_family_path("", i));
    }
    return paths;
}

uint32_t DataMmapInterface::segment_count() const {
    if (is_ready()) {
        return seg_idx_ + 1;
    }

    uint32_t count = 0;
    uint32_t capacity = 0;
    read_header_metadata(count, capacity); // throws on error
    return count;
}

} // namespace mmap
} // namespace file_service