#include "mmap_data.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

#include "can_analyzer_log.h"
#include "can_parser.h"

namespace mmap {

/// @brief Throw at the ReadOnly mode if file not existed. Create mmap if not exist at ReadWrite mode
/// @param file_path 
/// @param mode  
DataMmapInterface<Access::ReadOnly>::DataMmapInterface(std::string file_path)
    : 
      header_(file_path + ".header", MMapAccess::ReadOnly),
      segments_(file_path, MMapAccess::ReadOnly)
{
    // write count is stored in header; no in-memory copy needed.
}

DataMmapInterface<Access::ReadOnly>::~DataMmapInterface() = default;

std::string DataMmapInterface<Access::ReadOnly>::source_file_path() const
{
    std::string_view sv = header_.source_file_path();
    return std::string(sv.data(), sv.size());
}

DataMmapInterface<Access::ReadWrite>::DataMmapInterface(std::string file_path)
    : 
      header_(file_path + ".header", MMapAccess::ReadWrite),
      segments_(file_path, MMapAccess::ReadWrite)
{
    // header_.set_write_count(0);
    // header_.set_status(PARSER_STATUS_RUNNING);
}

DataMmapInterface<Access::ReadWrite>::~DataMmapInterface() = default;

std::string DataMmapInterface<Access::ReadWrite>::source_file_path() const
{
    std::string_view sv = header_.source_file_path();
    return std::string(sv.data(), sv.size());
}

void DataMmapInterface<Access::ReadWrite>::set_source_file_path(const std::string& path)
{
    // Forward to the header view which enforces read/write access.
    header_.set_source_file_path(path);
}

/// #NOTE: #TODO: could be optimize by return the page view pointer, then the caller construct with 1 pass 
std::vector<ParsedEntry>
DataMmapInterface<Access::ReadOnly>::read_page(
    uint64_t offset,
    uint64_t count) const
{
    std::vector<ParsedEntry> out_entries;
    if (count == 0) {
        return out_entries;
    }

    const uint64_t total_rows = header_.write_count();

    if (offset > total_rows) {
        throw MMapError(
            "page [" + std::to_string(offset) + ", "
            + std::to_string(offset + count)
            + ") is out of range (total rows = "
            + std::to_string(total_rows) + ")");
    }

    const uint64_t end_exclusive = (offset + count > total_rows) ? total_rows : (offset + count);
    const uint64_t actual_count = (end_exclusive > offset) ? (end_exclusive - offset) : 0;

    out_entries.reserve(static_cast<size_t>(actual_count));

    for (uint64_t row = offset; row < end_exclusive; ++row) {
        out_entries.emplace_back(segments_.read_at(row));
    }

    return out_entries;
}

/// @brief  A std::span<const uint32_t> is implicitly constructible from a std::vector<uint32_t> (and several other contiguous containers).
/// @param row_ids 
/// @return O(K)
std::vector<ParsedEntry>
DataMmapInterface<Access::ReadOnly>::read_rows(
    const std::vector<uint32_t>& row_ids) const
{
    std::vector<ParsedEntry> out_entries;
    out_entries.reserve(row_ids.size());

    for (uint32_t row : row_ids) {
        ParsedEntry entry;
        read_entry_at(row, entry);
        out_entries.push_back(std::move(entry));
    }

    return out_entries;
}

const ParsedEntry& DataMmapInterface<Access::ReadOnly>::read_entry_at(uint64_t row_index) const
{
    const uint64_t total_rows = header_.write_count();
    return segments_.read_at(row_index);
}

void DataMmapInterface<Access::ReadOnly>::read_entry_at(uint64_t row_index,
                                   ParsedEntry& out_entry) const
{
    const uint64_t total_rows = header_.write_count();
    if (row_index >= total_rows) {
        throw MMapError("row index out of range: " + std::to_string(row_index));
    }
    const ParsedEntry& src = segments_.read_at(row_index);
    out_entry = src;
}

uint32_t DataMmapInterface<Access::ReadWrite>::append_entry(const ParsedEntry& entry)
{
    const uint64_t idx = segments_.write_next(entry);
    const uint32_t row_index = static_cast<uint32_t>(idx);
    header_.set_write_count(static_cast<int32_t>(row_index + 1));
    return row_index;
}

void DataMmapInterface<Access::ReadWrite>::update_entry(
    uint32_t row_index,
    const ParsedEntry& record)
{
    if (static_cast<uint64_t>(row_index) >= header_.write_count()) {
        throw MMapError("row index out of range");
    }

    segments_.write_at(row_index, record);
}

} // namespace mmap
