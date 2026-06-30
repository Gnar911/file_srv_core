
#include "parsed_mmap_if.h"

#include <cstdint>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "can_analyzer_log.h"
#include "mmap/mmap_error_codes.h"

namespace file_service {
namespace {

std::vector<uint64_t> to_u64_rows(const std::vector<uint32_t>& rows) {
    std::vector<uint64_t> out;
    out.reserve(rows.size());
    for (uint32_t row : rows) {
        out.push_back(static_cast<uint64_t>(row));
    }
    return out;
}

std::pair<int64_t, int64_t> to_page_window(int64_t first, int64_t last) {
    if (last < first) {
        return {first, 0};
    }
    return {first, (last - first) + 1};
}

} // namespace

ParsedMmapInterface::ParsedMmapInterface(std::string token_id)
        : token_id_(std::move(token_id)),
            data_(token_id_),
            canid_(token_id_ + ".index"),
            channel_(token_id_ + ".index"),
            direction_(token_id_ + ".index") {}

int32_t ParsedMmapInterface::last_error_code() const {
    return last_error_code_;
}

void ParsedMmapInterface::clear_last_error() const {
    last_error_code_ = 0;
}

bool ParsedMmapInterface::is_segment_writers_ready() const {
    return initialized_
        && data_.is_ready()
        && canid_.is_ready()
        && channel_.is_ready()
        && direction_.is_ready();
}

void ParsedMmapInterface::reset_runtime_only() {
    buckets_ = file_service::mmap::IndexBuckets{};
}

int32_t ParsedMmapInterface::open_mmap() {
    CBCM_TRACE("ParsedMmapInterface::open_mmap enter token=%s", token_id_.c_str());
    clear_last_error();
    initialized_ = false;
    reset_runtime_only();

    int32_t rc = data_.open_and_init();
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::open_mmap data open failed rc=%d", rc);
        return rc;
    }

    rc = direction_.open_and_init();
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::open_mmap direction open failed rc=%d", rc);
        close_mmap();
        return rc;
    }

    rc = channel_.open_and_init();
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::open_mmap channel open failed rc=%d", rc);
        close_mmap();
        return rc;
    }

    rc = canid_.open_and_init();
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::open_mmap canid open failed rc=%d", rc);
        close_mmap();
        return rc;
    }

    initialized_ = true;
    CBCM_TRACE("ParsedMmapInterface::open_mmap exit rc=0 token=%s", token_id_.c_str());
    return 0;
}

int32_t ParsedMmapInterface::write_entries(const std::vector<LogRecord>& entries) {
    clear_last_error();
    if (!is_segment_writers_ready()) {
        last_error_code_ = file_service::mmap::error_code::kWriterNotReady;
        CBCM_ERROR("ParsedMmapInterface::write_entries writer not ready");
        return file_service::mmap::error_code::kWriterNotReady;
    }

    int32_t rc = data_.write_entries(entries, buckets_);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::write_entries data write failed rc=%d", rc);
        return rc;
    }

    rc = direction_.write_from_buckets(buckets_);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::write_entries direction write failed rc=%d", rc);
        return rc;
    }

    rc = channel_.write_from_buckets(buckets_);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::write_entries channel write failed rc=%d", rc);
        return rc;
    }

    rc = canid_.write_from_buckets(buckets_);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::write_entries canid write failed rc=%d", rc);
        return rc;
    }

    return 0;
}

void ParsedMmapInterface::close_mmap() {
    CBCM_TRACE("ParsedMmapInterface::close_mmap enter token=%s", token_id_.c_str());
    data_.close_and_finalize();
    direction_.close_and_finalize();
    channel_.close_and_finalize();
    canid_.close_and_finalize();
    initialized_ = false;
    CBCM_TRACE("ParsedMmapInterface::close_mmap exit token=%s", token_id_.c_str());
}

uint64_t ParsedMmapInterface::fetch_count() const {
    clear_last_error();
    uint64_t total_rows = 0;
    const int32_t rc = data_.read_total_rows(total_rows);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::fetch_count read_total_rows failed rc=%d", rc);
        return 0;
    }
    return total_rows;
}

int32_t ParsedMmapInterface::get_first_last_timestamp(double& out_first_ts,
                                                      double& out_last_ts) const {
    clear_last_error();
    const int32_t rc = data_.read_first_last_timestamp(out_first_ts, out_last_ts);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::get_first_last_timestamp failed rc=%d", rc);
        return rc;
    }
    return 0;
}

const std::string& ParsedMmapInterface::token_path() const {
    return token_id_;
}

std::vector<ParsedEntry> ParsedMmapInterface::read_all_entries() const {
    clear_last_error();
    std::vector<ParsedEntry> entries;
    const int32_t rc = data_.read_all_entries(entries);
    if (rc != 0) {
        last_error_code_ = rc;
        CBCM_ERROR("ParsedMmapInterface::read_all_entries failed rc=%d", rc);
        return {};
    }
    return entries;
}

std::vector<ParsedEntry> ParsedMmapInterface::read_rows_from_data(const std::vector<uint64_t>& rows) const {
    clear_last_error();
    if (rows.empty()) {
        last_error_code_ = file_service::mmap::error_code::kReadRowsEmptyRequest;
        CBCM_ERROR("ParsedMmapInterface::read_rows_from_data empty row request");
        return {};
    }

    uint64_t total_rows = 0;
    const int32_t total_rc = data_.read_total_rows(total_rows);
    if (total_rc != 0 || total_rows == 0) {
        last_error_code_ = (total_rc != 0) ? total_rc : file_service::mmap::error_code::kReadRowsNoData;
        CBCM_ERROR("ParsedMmapInterface::read_rows_from_data total_rows failed rc=%d total_rows=%llu",
                   total_rc,
                   static_cast<unsigned long long>(total_rows));
        return {};
    }

    std::vector<uint64_t> valid_rows;
    valid_rows.reserve(rows.size());
    for (uint64_t row : rows) {
        if (row < total_rows) {
            valid_rows.push_back(row);
        }
    }
    if (valid_rows.empty()) {
        last_error_code_ = file_service::mmap::error_code::kReadRowsOutOfRange;
        CBCM_ERROR("ParsedMmapInterface::read_rows_from_data all rows out of range requested=%zu total_rows=%llu",
                   rows.size(),
                   static_cast<unsigned long long>(total_rows));
        return {};
    }

    std::vector<ParsedEntry> entries;
    const int32_t read_rc = data_.read_entries(valid_rows, entries);
    if (read_rc != 0) {
        last_error_code_ = read_rc;
        CBCM_ERROR("ParsedMmapInterface::read_rows_from_data read_entries failed rc=%d", read_rc);
        return {};
    }
    if (entries.empty()) {
        last_error_code_ = file_service::mmap::error_code::kReadRowsEmptyResult;
        CBCM_ERROR("ParsedMmapInterface::read_rows_from_data read_entries returned 0 entries");
    }
    return entries;
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page(int64_t first, int64_t last) const {
    const auto [first_line, page_size] = to_page_window(first, last);
    const uint64_t start = first_line > 0 ? static_cast<uint64_t>(first_line) : 0ULL;
    const uint64_t size = page_size > 0 ? static_cast<uint64_t>(page_size) : 0ULL;
    std::vector<uint64_t> rows;
    rows.reserve(static_cast<size_t>(size));
    for (uint64_t i = 0; i < size; ++i) {
        rows.push_back(start + i);
    }
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_can_id(
    uint32_t can_id,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> page_rows = canid_.get_page_from_can_id_row_indices(can_id, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(page_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_can_ids(
    const std::vector<uint32_t>& can_ids,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> merged_rows = canid_.get_page_from_can_ids_row_indices(can_ids, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(merged_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_can_id_changed(
    uint32_t can_id,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> page_rows = canid_.get_page_from_can_id_changed_row_indices(can_id, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(page_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_can_ids_changed(
    const std::vector<uint32_t>& can_ids,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> merged_rows = canid_.get_page_from_can_ids_changed_row_indices(can_ids, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(merged_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_channel(
    const std::string& channel,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> page_rows = channel_.get_page_from_channel_row_indices(channel, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(page_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_channels(
    const std::vector<std::string>& channels,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> merged_rows = channel_.get_page_from_channels_row_indices(channels, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(merged_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_direction(
    const std::string& direction,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> page_rows = direction_.get_page_from_direction_row_indices(direction, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(page_rows);
    return read_rows_from_data(rows);
}

std::vector<ParsedEntry> ParsedMmapInterface::read_page_from_directions(
    const std::vector<std::string>& directions,
    int64_t first,
    int64_t last) {
    const auto [first_line, page_size] = to_page_window(first, last);
    const std::vector<uint32_t> merged_rows = direction_.get_page_from_directions_row_indices(directions, first_line, page_size);
    const std::vector<uint64_t> rows = to_u64_rows(merged_rows);
    return read_rows_from_data(rows);
}

std::vector<std::string> ParsedMmapInterface::data_segment_paths() const {
    return data_.segment_paths();
}

std::vector<std::string> ParsedMmapInterface::canid_segment_paths() const {
    return canid_.segment_paths();
}

std::vector<std::string> ParsedMmapInterface::channel_segment_paths() const {
    return channel_.segment_paths();
}

std::vector<std::string> ParsedMmapInterface::direction_segment_paths() const {
    return direction_.segment_paths();
}

ParsedMmapSegmentPaths ParsedMmapInterface::all_segment_paths() const {
    ParsedMmapSegmentPaths paths;
    paths.data = data_segment_paths();
    paths.canid = canid_segment_paths();
    paths.channel = channel_segment_paths();
    paths.direction = direction_segment_paths();
    return paths;
}

} // namespace file_service