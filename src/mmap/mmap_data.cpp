#include "mmap_data.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

#include "can_analyzer_log.h"
#include "can_parser.h"
#include "mmap_error_codes.h"

namespace file_service {
namespace mmap {

DataMmapInterface::DataMmapInterface(std::string base)
    : base_(std::move(base)) {}

int32_t DataMmapInterface::read_header_metadata(uint32_t& out_segment_count,
                                                uint32_t& out_capacity) const {
    out_segment_count = 0;
    out_capacity = 0;

    const std::string header0_path = make_segment_family_path("", 0);
    MMapHandle handle = {};
    if (!mmap_open_ro(header0_path.c_str(), handle)) {
        CBCM_ERROR("ensure_reader_state_loaded failed: header .000 not found path='%s'", header0_path.c_str());
        return file_service::mmap::error_code::kHeaderSegment0Missing;
    }

    if (handle.addr == nullptr || handle.size < file_service::kMmapHeaderConstractSize) {
        CBCM_ERROR("ensure_reader_state_loaded failed: invalid header map path='%s' size=%zu",
                   header0_path.c_str(),
                   handle.size);
        mmap_close(handle);
        return file_service::mmap::error_code::kHeaderInvalid;
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    out_segment_count = hdr->segment_count;
    out_capacity = hdr->capacity;
    mmap_close(handle);

    if (out_segment_count == 0 || out_capacity == 0) {
        CBCM_ERROR("ensure_reader_state_loaded failed: zero metadata segment_count=%u capacity=%u path='%s'",
                   out_segment_count,
                   out_capacity,
                   header0_path.c_str());
        return file_service::mmap::error_code::kHeaderZeroMetadata;
    }

    return file_service::mmap::error_code::kOk;
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

int32_t DataMmapInterface::read_entry(uint64_t global_row,
                                      ParsedEntry& out_entry) const {
    uint32_t segment_count = 0;
    uint32_t mmap_capacity = 0;
    const int32_t state_rc = read_header_metadata(segment_count, mmap_capacity);
    if (state_rc != 0) {
        return state_rc;
    }

    const uint64_t seg_idx = global_row / static_cast<uint64_t>(mmap_capacity);
    const uint64_t local_idx = global_row % static_cast<uint64_t>(mmap_capacity);
    if (seg_idx >= segment_count) {
        CBCM_ERROR("read_entry failed: seg_idx out of range global_row=%llu seg_idx=%llu segment_count=%u",
                   static_cast<unsigned long long>(global_row),
                   static_cast<unsigned long long>(seg_idx),
                   segment_count);
        return file_service::mmap::error_code::kSegmentIndexOutOfRange;
    }

    return read_entry_from_segment(static_cast<uint32_t>(seg_idx), local_idx, out_entry);
}

int32_t DataMmapInterface::read_entry_from_segment(uint32_t seg_idx,
                                                   uint64_t target_idx,
                                                   ParsedEntry& out_entry) const {
    const std::string path = make_segment_family_path("", seg_idx);
    MMapHandle handle = {};
    if (!mmap_open_ro(path.c_str(), handle)) {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment mmap not found/open failed path='%s'", path.c_str());
        return file_service::mmap::error_code::kMmapOpenFailed;
    }

    if (handle.addr == nullptr || handle.size < file_service::kMmapHeaderConstractSize) {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment invalid header path='%s' size=%zu",
                   path.c_str(),
                   handle.size);
        mmap_close(handle);
        return file_service::mmap::error_code::kHeaderInvalid;
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    if (hdr->capacity == 0 || target_idx >= hdr->capacity) {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment invalid index path='%s' target_idx=%llu capacity=%u",
                   path.c_str(),
                   static_cast<unsigned long long>(target_idx),
                   hdr->capacity);
        mmap_close(handle);
        return file_service::mmap::error_code::kInvalidReadArg;
    }

    const size_t offset = file_service::kMmapHeaderConstractSize
        + static_cast<size_t>(target_idx) * static_cast<size_t>(kParsedEntrySize);
    const bool ok = offset + static_cast<size_t>(kParsedEntrySize) <= handle.size;
    if (ok) {
        std::memcpy(&out_entry,
                    reinterpret_cast<const uint8_t*>(handle.addr) + offset,
                    static_cast<size_t>(kParsedEntrySize));
    } else {
        CBCM_ERROR("DataMmapInterface::read_entry_from_segment out-of-range path='%s' offset=%zu entry_size=%zu handle_size=%zu",
                   path.c_str(),
                   offset,
                   static_cast<size_t>(kParsedEntrySize),
                   handle.size);
    }
    mmap_close(handle);
    return ok ? file_service::mmap::error_code::kOk : file_service::mmap::error_code::kReadOutOfRange;
}

int32_t DataMmapInterface::read_entries(const std::vector<uint64_t>& rows,
                                        std::vector<ParsedEntry>& out_entries) const {
    out_entries.clear();
    if (rows.empty()) {
        return file_service::mmap::error_code::kOk;
    }

    out_entries.reserve(rows.size());
    for (uint64_t row : rows) {
        ParsedEntry entry;
        const int32_t rc = read_entry(row, entry);
        if (rc != 0) {
            return rc;
        }
        out_entries.push_back(entry);
    }

    return file_service::mmap::error_code::kOk;
}

int32_t DataMmapInterface::read_all_entries(
    std::vector<ParsedEntry>& out_entries) const
{
    out_entries.clear();

    uint32_t segment_count;
    uint32_t mmap_capacity;

    int32_t rc = read_header_metadata(segment_count, mmap_capacity);
    if (rc != 0)
        return rc;

    for (uint32_t seg = 0; seg < segment_count; ++seg)
    {
        rc = append_segment_entries(seg, out_entries);
        if (rc != 0)
            return rc;
    }

    return file_service::mmap::error_code::kOk;
}

int32_t DataMmapInterface::append_segment_entries(
    uint32_t seg_idx,
    std::vector<ParsedEntry>& out_entries) const
{
    const std::string path = make_segment_family_path("", seg_idx);

    MMapHandle handle{};
    if (!mmap_open_ro(path.c_str(), handle))
        return file_service::mmap::error_code::kMmapOpenFailed;

    auto* hdr =
        reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);

    auto* entries =
        reinterpret_cast<const ParsedEntry*>(
            reinterpret_cast<const uint8_t*>(handle.addr)
            + file_service::kMmapHeaderConstractSize);

    out_entries.insert(
        out_entries.end(),
        entries,
        entries + hdr->write_count);      // or hdr->size / valid_count
                                          // whatever represents valid entries

    mmap_close(handle);

    return file_service::mmap::error_code::kOk;
}

int32_t DataMmapInterface::read_first_last_timestamp(double& out_first_ts,
                                                     double& out_last_ts) const {
    out_first_ts = 0.0;
    out_last_ts = 0.0;

    uint64_t total_rows = 0;
    const int32_t total_rc = read_total_rows(total_rows);
    if (total_rc != 0) {
        return total_rc;
    }
    if (total_rows == 0) {
        return file_service::mmap::error_code::kReadRowsNoData;
    }

    const int32_t first_rc = read_timestamp(0, out_first_ts);
    if (first_rc != 0) {
        return first_rc;
    }

    const int32_t last_rc = read_timestamp(total_rows - 1, out_last_ts);
    if (last_rc != 0) {
        return last_rc;
    }

    return file_service::mmap::error_code::kOk;
}

uint64_t DataMmapInterface::timestamp_lower_bound(uint64_t total_rows,
                                                double target_ts) const {
    uint64_t lo = 0;
    uint64_t hi = total_rows;
    while (lo < hi) {
        const uint64_t mid = (lo + hi) / 2;
        double ts = 0.0;
        const bool ok = (read_timestamp(mid, ts) == 0);
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
        const bool ok = (read_timestamp(mid, ts) == 0);
        if (!ok || ts <= target_ts) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

int32_t DataMmapInterface::read_timestamp(uint64_t global_row,
                                          double& out_ts) const {
    ParsedEntry entry;
    const int32_t rc = read_entry(global_row, entry);
    if (rc != 0) {
        return rc;
    }
    out_ts = entry.timestamp;
    return file_service::mmap::error_code::kOk;
}

int32_t DataMmapInterface::read_total_rows(uint64_t& out_total_rows) const {
    out_total_rows = 0;
    uint32_t segment_count = 0;
    uint32_t mmap_capacity = 0;
    const int32_t state_rc = read_header_metadata(segment_count, mmap_capacity);
    if (state_rc != 0) {
        return state_rc;
    }

    uint64_t total = 0;
    for (uint32_t seg_idx = 0; seg_idx < segment_count; ++seg_idx) {
        uint64_t count = 0;
        const int32_t rc = read_segment_write_count(make_segment_family_path("", seg_idx), count);
        if (rc != 0) {
            return rc;
        }
        total += count;
    }
    out_total_rows = total;
    return 0;
}

int32_t DataMmapInterface::read_segment_write_count(const std::string& segment_path,
                                                    uint64_t& out_count) const {
    out_count = 0;
    if (segment_path.empty()) {
        return file_service::mmap::error_code::kEmptySegmentPath;
    }

    MMapHandle handle = {};
    if (!mmap_open_ro(segment_path.c_str(), handle)) {
        CBCM_ERROR("read_segment_write_count mmap not found/open failed path='%s'", segment_path.c_str());
        return file_service::mmap::error_code::kReadCountOpenFailed;
    }
    if (handle.size < file_service::kMmapHeaderConstractSize) {
        mmap_close(handle);
        return file_service::mmap::error_code::kReadCountHeaderTooSmall;
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    out_count = hdr->write_count;
    mmap_close(handle);
    return file_service::mmap::error_code::kOk;
}

int32_t DataMmapInterface::read_segment_capacity(const std::string& segment_path,
                                                 uint32_t& out_capacity) const {
    out_capacity = 0;
    if (segment_path.empty()) {
        return file_service::mmap::error_code::kEmptyCapacityPath;
    }

    MMapHandle handle = {};
    if (!mmap_open_ro(segment_path.c_str(), handle)) {
        CBCM_ERROR("read_segment_capacity mmap not found/open failed path='%s'", segment_path.c_str());
        return file_service::mmap::error_code::kReadCapacityOpenFailed;
    }
    if (handle.size < file_service::kMmapHeaderConstractSize) {
        mmap_close(handle);
        return file_service::mmap::error_code::kReadCapacityHeaderTooSmall;
    }

    const auto* hdr = reinterpret_cast<const file_service::MmapHeaderConstract*>(handle.addr);
    out_capacity = hdr->capacity;
    mmap_close(handle);
    return file_service::mmap::error_code::kOk;
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
    last_timestamp_by_id_ = LastTimestampTable{};
    last_raw_by_id_.clear();
    channel_to_index_.clear();
}

bool DataMmapInterface::is_ready() const {
    return seg_handle_.addr != nullptr
        && seg_hdr_ != nullptr
        && seg_entries_ != nullptr;
}

bool DataMmapInterface::open_segment(uint32_t index) {
    CBCM_TRACE("DataMmapInterface::open_segment index=%u", index);
    const std::string seg_path = make_segment_family_path("", index);
    const size_t seg_size = file_service::kMmapHeaderConstractSize
        + static_cast<size_t>(kDataSegmentCapacity) * sizeof(ParsedEntry);
    if (!mmap_create_rw(seg_path.c_str(), seg_size, seg_handle_)) {
        CBCM_ERROR("DataMmapInterface::open_segment mmap_create_rw failed path='%s' size=%zu",
                   seg_path.c_str(),
                   seg_size);
        return false;
    }
    seg_hdr_ = reinterpret_cast<file_service::MmapHeaderConstract*>(seg_handle_.addr);
    seg_entries_ = reinterpret_cast<ParsedEntry*>(
        reinterpret_cast<uint8_t*>(seg_handle_.addr) + file_service::kMmapHeaderConstractSize);
    file_service::init_mmap_header_constract(*seg_hdr_, kDataSegmentCapacity, PARSER_STATUS_RUNNING, index + 1);
    seg_write_ = 0;
    return true;
}

int32_t DataMmapInterface::open_and_init() {
    reset();
    seg_idx_ = 0;
    if (!open_segment(seg_idx_)) {
        CBCM_ERROR("DataMmapInterface::open_and_init failed at first segment");
        return file_service::mmap::error_code::kOpenInitFailed;
    }
    return file_service::mmap::error_code::kOk;
}

int32_t DataMmapInterface::write_entries(const std::vector<ParsedEntry>& parsed_entries,
                                       IndexBuckets& buckets) {
    if (!is_ready()) {
        return file_service::mmap::error_code::kWriterNotReady;
    }

    for (const ParsedEntry& entry : parsed_entries) {
        if (seg_write_ >= kDataSegmentCapacity) {
            close_and_finalize();
            ++seg_idx_;
            if (!open_segment(seg_idx_)) {
                return file_service::mmap::error_code::kOpenNextSegmentFailed;
            }
        }

        ParsedEntry out_entry = entry;
        out_entry.last_timestamp = last_timestamp_by_id_.update_and_get_prev(out_entry.can_id, out_entry.timestamp);

        auto it = last_raw_by_id_.find(out_entry.can_id);
        if (it == last_raw_by_id_.end()) {
            out_entry.changed = 0;
            PrevRaw prev;
            prev.len = out_entry.data_len;
            if (out_entry.data_len > 0) {
                std::memcpy(prev.data, out_entry.data, out_entry.data_len);
            }
            last_raw_by_id_.emplace(out_entry.can_id, prev);
        } else {
            const PrevRaw& prev = it->second;
            const bool changed = (prev.len != out_entry.data_len)
                || (out_entry.data_len > 0 && std::memcmp(prev.data, out_entry.data, out_entry.data_len) != 0);
            out_entry.changed = changed ? 1 : 0;
            it->second.len = out_entry.data_len;
            if (out_entry.data_len > 0) {
                std::memcpy(it->second.data, out_entry.data, out_entry.data_len);
            }
        }

        seg_entries_[seg_write_] = out_entry;
        const uint32_t row_idx = global_row_idx_++;
        buckets.can_id_rows[out_entry.can_id].push_back(row_idx);
        if (out_entry.changed == 1) {
            buckets.can_id_changed_rows[out_entry.can_id].push_back(row_idx);
        }
        buckets.can_id_timestamps[out_entry.can_id].push_back(out_entry.timestamp);

        const std::string channel_key = normalize_channel_key(out_entry.channel);
        auto ch_it = channel_to_index_.find(channel_key);
        uint8_t channel_idx = 0;
        if (ch_it == channel_to_index_.end()) {
            channel_idx = static_cast<uint8_t>(buckets.channel_table.size());
            channel_to_index_.emplace(channel_key, channel_idx);
            buckets.channel_table.push_back(channel_key);
            buckets.channel_rows.emplace_back();
        } else {
            channel_idx = ch_it->second;
        }
        if (channel_idx < buckets.channel_rows.size()) {
            buckets.channel_rows[channel_idx].push_back(row_idx);
        }
        buckets.direction_rows[(out_entry.direction == 0) ? 0 : 1].push_back(row_idx);

        ++seg_write_;
        seg_hdr_->write_count = seg_write_;
        ++total_written_;
    }

    return file_service::mmap::error_code::kOk;
}

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
    if (read_header_metadata(count, capacity) != 0) {
        return 0;
    }
    return count;
}

} // namespace mmap
} // namespace file_service