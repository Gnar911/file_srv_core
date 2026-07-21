#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mmap/mmap_data.h"
#include "parsed_entry_layout.h"
#include "sqlite/log_index_db.h"


/**
 * @brief Lazy logical view over mmap-backed parsed log entries.
 *
 * ViewBrowser does NOT own or materialize ParsedEntry objects.
 *
 * For a filtered view, row_indices_ stores the mapping:
 *
 *      logical index       physical mmap row
 *
 *           0        ->           7
 *           1        ->          31
 *           2        ->          46
 *           3        ->         102
 *
 * Access:
 *
 *      browser.at(2)
 *          -> row_indices_[2]
 *          -> 46
 *          -> data_mmap.read_entry_at(46)
 *
 * Both mapping lookup and mmap random access are O(1).
 *
 * For an unfiltered view, no identity index [0, 1, 2, ...] is allocated.
 * The logical index is directly the physical mmap row index.
 *
 * Lifetime:
 *      The referenced DataMmapInterface must outlive this ViewBrowser.
 *
 * Thread safety:
 *      ViewBrowser is immutable after construction. Concurrent reads are safe
 *      provided DataMmapInterface::read_entry_at() supports concurrent reads
 *      and the underlying mmap is not invalidated/remapped concurrently.
 */
class ViewBrowser {
public:
    explicit ViewBrowser(
        const mmap::DataMmapInterface<mmap::Access::ReadOnly>& data
    )
        : data_(data)
    {
    }

    /// @brief  TODO: Make private friend class
    /// @param row_count 
    void set_full_view(uint64_t row_count);
    void set_rows(std::vector<uint32_t> rows);

    const ParsedEntry& at(uint64_t logical_index) const;
    size_t size() const noexcept;

private:
    const mmap::DataMmapInterface<mmap::Access::ReadOnly>& data_;
    std::vector<uint32_t> rows_;
    uint64_t row_count_ = 0;
    bool full_view_ = true;
};