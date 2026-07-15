#include "metadata_storage_if.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "can_analyzer_log.h"
//#include "mmap/mmap_error_codes.h"
#include <cstring>
#include <iostream>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

std::pair<int32_t, int32_t> to_page_window(int32_t first, int32_t last) {
    if (last < first) {
        return {first, 0};
    }
    return {first, (last - first) + 1};
}

} // namespace

MetaDataStorageInterface::MetaDataStorageInterface(std::string id)
    : token_id(std::move(id)),
      storage_token_(token_id),
      wdata_(storage_token_.mmap_path().string()),
      rdata_(storage_token_.mmap_path().string()),
      index_db_(storage_token_.sqlite_path().string())
{
    // Diagnostic prints to help identify environment differences between
    // the main (Python) process and the parser child process.
//     try {
// #if defined(_WIN32)
//         std::cout << "PID = <win>" << std::endl;
// #else
//         std::cout << "PID = " << getpid() << std::endl;
// #endif
//         const char* t = std::getenv("TMPDIR");
//         std::cout << "TMPDIR = " << (t ? t : "<null>") << std::endl;
//         try {
//             std::cout << "temp = " << std::filesystem::temp_directory_path() << std::endl;
//         } catch (const std::exception& e) {
//             std::cout << "temp = <error> " << e.what() << std::endl;
//         }
//         std::cout << "StorageToken.root=" << storage_token_.root() << std::endl;
//         std::cout << "StorageToken.sqlite_path=" << storage_token_.sqlite_path() << std::endl;
//         std::cout << "StorageToken.mmap_path=" << storage_token_.mmap_path() << std::endl;
//     } catch (...) {}
}

void MetaDataStorageInterface::set_file_path(const std::string& path)
{
    wdata_.set_source_file_path(path);
}

// int32_t MetaDataStorageInterface::last_error_code() const {
//     return last_error_code_;
// }

// void MetaDataStorageInterface::clear_last_error() const {
//     last_error_code_ = 0;
// }


// open_storage/close_storage removed: RAII in MetaDataStorageInterface + LogIndexDatabase handles lifecycle

static bool payload_changed(const LogRecord& prev,
                            const LogRecord& cur)
{
    const uint8_t prev_len =
        std::min(prev.data_len,
                 static_cast<uint8_t>(sizeof(prev.data)));

    const uint8_t cur_len =
        std::min(cur.data_len,
                 static_cast<uint8_t>(sizeof(cur.data)));

    if (prev_len != cur_len)
    {
        return true;
    }

    return cur_len > 0 &&
           std::memcmp(prev.data, cur.data, cur_len) != 0;
}

/* Write entries with 1 pass with many storages technical*/
/// 20260709 TODO: Add the 1 pass write_next for high integrated with the other module
void MetaDataStorageInterface::write_entries(const std::vector<LogRecord>& entries) {
    //clear_last_error();

    index_db_.begin_transaction();
    for (const auto& entry : entries)
    {
        ParsedEntry parsed = {};
        std::memcpy(&parsed, &entry, sizeof(LogRecord));
        /// NOTE: Instead of storing the prev of all metadata -> store the row index only, other will be derived
        // const auto meta = tracker_.update(
        //     entry.can_id,
        //     entry.data,
        //     entry.data_len,
        //     entry.timestamp);
        // parsed.changed = meta.changed ? 1 : 0;
        // parsed.last_timestamp = meta.last_timestamp;

        int32_t row_index = wdata_.next_write_idx();

        parsed.prev_same_can = tracker_.append(row_index, entry.can_id);

        // Next is still inknown so just let it kInvalidRow
        parsed.next_same_can = kInvalidRow;

        // At the first message frame
        if (parsed.prev_same_can == kInvalidRow)
        {
            parsed.changed = 0;
            parsed.last_timestamp = entry.timestamp;
        }    
        else
        {
            /// Calculate last timestamp for this entry
            const ParsedEntry& prev =
                rdata_.read_entry_at(parsed.prev_same_can);

            parsed.last_timestamp = prev.timestamp;
            parsed.changed = payload_changed(prev, parsed);
        }

        wdata_.append_entry(parsed);
        index_db_.append_index(row_index, parsed);
    }
    index_db_.commit_transaction();
}

// void MetaDataStorageInterface::update_entry(uint32_t row_index,
//                       const LogRecord& entry) {

//     index_db_.update_index(row_index, u.record,
//         [&data = this->rdata_](uint32_t idx) -> LogRecord {
//             LogRecord rec{};
//             data.read_entry(idx, rec);
//             return rec;
//         });
//     wdata_.update_entry(row_index, entry);
// }
void MetaDataStorageInterface::update_entry(
    uint32_t row_index,
    const LogRecord& record)
{
    ParsedEntry current{};
    rdata_.read_entry_at(row_index, current);

    //
    // Update the raw CAN frame.
    //
    /// +----------------------+
    // | LogRecord            |  <-- memcpy overwrites this part
    // +----------------------+
    // | line_number          |  <-- untouched
    // +----------------------+
    // | last_timestamp       |  <-- untouched
    // +----------------------+
    // | changed              |  <-- untouched
    // +----------------------+
    // | prev_same_can        |  <-- untouched
    // +----------------------+
    // | next_same_can        |  <-- untouched
    // +----------------------+
    /// NOTE: Could use idomatic here
    /// static_cast<LogRecord&>(current) = record;
    /// LogRecord& base = current;
    /// base = record;
    std::memcpy(static_cast<LogRecord*>(&current),
                &record,
                sizeof(LogRecord));

    //
    // Recompute metadata from the previous node.
    //
    if (current.prev_same_can == kInvalidRow)
    {
        current.changed = 0;
        current.last_timestamp = current.timestamp;
    }
    else
    {
        ParsedEntry prev{};
        rdata_.read_entry_at(current.prev_same_can, prev);

        current.last_timestamp = prev.timestamp;

        current.changed =
            payload_changed(prev, current) ? 1 : 0;
    }

    //
    // Persist current row.
    //
    wdata_.update_entry(row_index, current);
    index_db_.update_index(row_index, current);

    //
    // Repair the next node because its comparison
    // depends on the current payload.
    //
    if (current.next_same_can != kInvalidRow)
    {
        ParsedEntry next{};
        rdata_.read_entry_at(current.next_same_can, next);

        next.changed =
            payload_changed(current, next) ? 1 : 0;

        next.last_timestamp = current.timestamp;

        wdata_.update_entry(current.next_same_can, next);
        index_db_.update_index(current.next_same_can, next);
    }
}

// close_storage removed: RAII-managed cleanup occurs in destructors

uint32_t MetaDataStorageInterface::fetch_count() const {
    //clear_last_error();
    return index_db_.row_count();
}

std::string MetaDataStorageInterface::token_path() const {
    return storage_token_.sqlite_path().string();
}

bool MetaDataStorageInterface::get_first_last_timestamp(double& out_first_ts,
                                                        double& out_last_ts) const {
    //clear_last_error();
    return index_db_.get_first_last_timestamp(out_first_ts, out_last_ts);
}

// const std::string& MetaDataStorageInterface::token_path() const {
//     return token_id;
// }

// MetaDataStorageInterface::Metadata MetaDataStorageInterface::get_metadata() const {
//     Metadata m;
//     m.total_rows = fetch_count();
//     double first = 0, last = 0;
//     if (get_first_last_timestamp(first, last)) {
//         m.first_timestamp = first;
//         m.last_timestamp = last;
//     }
//     // prefer the header-stored source file path from the read mmap
//     try {
//         m.source_file_path = rdata_.source_file_path();
//     } catch (...) {
//         m.source_file_path.clear();
//     }
//     return m;
// }

std::vector<ParsedEntry> MetaDataStorageInterface::read_page(int32_t offset, int32_t count) const {
    return rdata_.read_page(offset, count);
}

/// @brief Read page with query function
/// @param query 
/// @param first 
/// @param last 
/// @return  
std::vector<ParsedEntry> MetaDataStorageInterface::read_page_multi(const LogQuery& query,
                                                                  int32_t first,
                                                                  int32_t last) {
    const std::vector<uint32_t> rows = index_db_.query_row_indices(query, first, last);
    if (rows.empty()) {
        return {};
    }
    return rdata_.read_rows(rows);
}

