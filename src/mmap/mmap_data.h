#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mmap_layout_view.h"
#include "parsed_entry_layout.h"

namespace mmap {

enum class Access {
    ReadOnly,
    ReadWrite,
};

template<Access access>
class DataMmapInterface;

template<>
class DataMmapInterface<Access::ReadOnly> {
public:
    explicit DataMmapInterface(std::string file_path);
    ~DataMmapInterface();

    DataMmapInterface(const DataMmapInterface&) = delete;
    DataMmapInterface& operator=(const DataMmapInterface&) = delete;
    DataMmapInterface(DataMmapInterface&&) = delete;
    DataMmapInterface& operator=(DataMmapInterface&&) = delete;

    // read APIs (work with raw ParsedEntry only)

    std::vector<ParsedEntry> read_page(
        uint64_t offset,
        uint64_t count) const;                        
    void read_entry_at(uint64_t row_index, ParsedEntry& out_entry) const;
    const ParsedEntry& read_entry_at(uint64_t row_index) const;

    /// NOTE: modern C++ 20 style
    std::vector<ParsedEntry> read_rows(const std::vector<uint32_t>& row_ids) const;

    // Return the source file path stored in the header (empty if not set).
    std::string source_file_path() const;

    // Set the source file path stored in the header (writeable variant).
    void set_source_file_path(const std::string& path);

private:
    // RAII views: opened at construction, released at destruction.
    mutable HeaderView header_;
    mutable SegmentView<ParsedEntry> segments_;
};

template<>
class DataMmapInterface<Access::ReadWrite> {
public:
    explicit DataMmapInterface(std::string file_path);
    ~DataMmapInterface();

    DataMmapInterface(const DataMmapInterface&) = delete;
    DataMmapInterface& operator=(const DataMmapInterface&) = delete;
    DataMmapInterface(DataMmapInterface&&) = delete;
    DataMmapInterface& operator=(DataMmapInterface&&) = delete;

    // write APIs
    uint32_t append_entry(const ParsedEntry& entry);
    void update_entry(uint32_t row_index, const ParsedEntry& record);
	int32_t next_write_idx() {return segments_.next_index();}	

    // Return the source file path stored in the header (empty if not set).
    std::string source_file_path() const;

    // Set the source file path in the header. Only available for write-mode.
    void set_source_file_path(const std::string& path);

private:
    //LastTimestampTable last_timestamp_by_id_;
    // RAII views: opened at construction, released at destruction.
    mutable HeaderView header_;
    mutable SegmentView<ParsedEntry> segments_;
    
};

} // namespace mmap
