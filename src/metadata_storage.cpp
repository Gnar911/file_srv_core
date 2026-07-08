#include "metadata_storage_if.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "can_analyzer_log.h"
#include "mmap/mmap_error_codes.h"
#include <cstring>

namespace file_service {
namespace {

std::pair<int64_t, int64_t> to_page_window(int64_t first, int64_t last) {
    if (last < first) {
        return {first, 0};
    }
    return {first, (last - first) + 1};
}

} // namespace

MetaDataStorageInterface::MetaDataStorageInterface(std::string mmap_prefix)
        : mmap_prefix_(std::move(mmap_prefix)),
            data_(mmap_prefix_),
            index_db_(mmap_prefix_) {}

int32_t MetaDataStorageInterface::last_error_code() const {
    return last_error_code_;
}

void MetaDataStorageInterface::clear_last_error() const {
    last_error_code_ = 0;
}

void MetaDataStorageInterface::set_file_path(std::string file_path) {
    data_.set_source_file_path(std::move(file_path));
}

std::string MetaDataStorageInterface::get_file_path() const {
    std::string path;
    data_.read_source_file_path(path);
    return path;
}

bool MetaDataStorageInterface::is_segment_writers_ready() const {
    return initialized_ && data_.is_ready();
}


void MetaDataStorageInterface::open_storage() {
    CBCM_TRACE("MetaDataStorageInterface::open_mmap enter token=%s", mmap_prefix_.c_str());
    clear_last_error();
    initialized_ = false;

    data_.open_and_init(); // throws on error
    // SQLite multi-factor filter index (payload stays in mmap; see header note).
    index_db_.open_append_session();
    //index_db_.initialize_schema();

    initialized_ = true;
}

/* Write entries with 1 pass with many storages technical*/
void MetaDataStorageInterface::write_entries(const std::vector<LogRecord>& entries) {
    clear_last_error();
    if (!is_segment_writers_ready()) {
        last_error_code_ = file_service::mmap::error_code::kWriterNotReady;
        throw std::runtime_error("MetaDataStorageInterface::write_entries writer not ready");
    }

    index_db_.begin_transaction();
    for (const auto& entry : entries)
    {
        const uint32_t row_index = data_.append_entry(entry);
        index_db_.append_entry(row_index, entry);
    }
    index_db_.commit_transaction();
}

// void MetaDataStorageInterface::write_entries(const std::vector<LogRecord>& entries) {
//     clear_last_error();
//     if (!is_segment_writers_ready()) {
//         last_error_code_ = file_service::mmap::error_code::kWriterNotReady;
//         throw std::runtime_error("MetaDataStorageInterface::write_entries writer not ready");
//     }

//     // Global row index of entries[0] for this batch (before the data write
//     // advances the counter). Used to key the SQLite index rows 1:1 with mmap.
//     const uint64_t start_row = data_.total_written();

//     data_.write_entries(entries); // allow exceptions to propagate

//     // Populate the SQLite filter index. "changed" comes from the same buckets the
//     // data writer produced, so the changed flag stays consistent with payload.
//     std::unordered_set<uint32_t> changed_rows;
//     const auto& buckets = data_.index_buckets();
//     for (const auto& kv : buckets.can_id_changed_rows) {
//         for (const uint32_t row : kv.second) {
//             changed_rows.insert(row);
//         }
//     }

//     index_db_.begin_transaction();
//     index_db_.append_entries(entries, start_row, changed_rows);
//     index_db_.commit_transaction();
// }

void MetaDataStorageInterface::update_entries(const std::vector<EntryUpdate>& entries) {
    clear_last_error();
    if (entries.empty()) {
        return;
    }

    if (!is_segment_writers_ready()) {
        last_error_code_ = file_service::mmap::error_code::kWriterNotReady;
        throw std::runtime_error("MetaDataStorageInterface::update_entries writer not ready");
    }

    // Cold path: recompute changed via SQLite neighbor lookup + mmap payload reads,
    // then overwrite the mmap payload row.
    for (const EntryUpdate& u : entries) {
        const uint32_t row_index = static_cast<uint32_t>(u.row_index);
        index_db_.update_entry(row_index, u.record, data_);
        data_.update_entry(row_index, u.record);
    }
}

void MetaDataStorageInterface::close_storage() {
    CBCM_TRACE("MetaDataStorageInterface::close_mmap enter token=%s", mmap_prefix_.c_str());
    data_.close_and_finalize();
    index_db_.close_append_session();
    initialized_ = false;
}

uint64_t MetaDataStorageInterface::fetch_count() const {
    clear_last_error();
    uint64_t total_rows = 0;
    data_.read_total_rows(total_rows);
    return total_rows;
}

void MetaDataStorageInterface::get_first_last_timestamp(double& out_first_ts,
                                                        double& out_last_ts) const {
    clear_last_error();
    data_.read_first_last_timestamp(out_first_ts, out_last_ts); // throws on error
}

const std::string& MetaDataStorageInterface::token_path() const {
    return mmap_prefix_;
}

std::vector<ParsedEntry> MetaDataStorageInterface::read_all_entries() const {
    clear_last_error();
    std::vector<ParsedEntry> entries;
    data_.read_all_entries(entries); // throws on error
    return entries;
}

std::vector<ParsedEntry> MetaDataStorageInterface::read_rows_from_data(const std::vector<uint64_t>& rows) const {
    clear_last_error();
    if (rows.empty()) {
        last_error_code_ = file_service::mmap::error_code::kReadRowsEmptyRequest;
        CBCM_ERROR("MetaDataStorageInterface::read_rows_from_data empty row request");
        return {};
    }

    uint64_t total_rows = 0;
    data_.read_total_rows(total_rows); // throws on error
    if (total_rows == 0) {
        last_error_code_ = file_service::mmap::error_code::kReadRowsNoData;
        CBCM_ERROR("MetaDataStorageInterface::read_rows_from_data total_rows failed total_rows=%llu",
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
        CBCM_ERROR("MetaDataStorageInterface::read_rows_from_data all rows out of range requested=%zu total_rows=%llu",
                   rows.size(),
                   static_cast<unsigned long long>(total_rows));
        return {};
    }

    std::vector<ParsedEntry> entries;
    data_.read_entries(valid_rows, entries); // throws on error
    if (entries.empty()) {
        last_error_code_ = file_service::mmap::error_code::kReadRowsEmptyResult;
        CBCM_ERROR("MetaDataStorageInterface::read_rows_from_data read_entries returned 0 entries");
    }
    return entries;
}

std::vector<ParsedEntry> MetaDataStorageInterface::read_page(int64_t first, int64_t last) const {
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

std::vector<ParsedEntry> MetaDataStorageInterface::read_page_multi(const LogQuery& query,
                                                                  int64_t first,
                                                                  int64_t last) {
    clear_last_error();
    // SQLite answers the multi-factor predicate with row indices; mmap fetches
    // the actual payload for those rows (see design note in metadata_storage_if.h).
    const std::vector<uint64_t> rows = index_db_.query_row_indices(query, first, last);
    if (rows.empty()) {
        return {};
    }
    return read_rows_from_data(rows);
}

std::vector<std::string> MetaDataStorageInterface::data_segment_paths() const {
    return data_.segment_paths();
}

} // namespace file_service
