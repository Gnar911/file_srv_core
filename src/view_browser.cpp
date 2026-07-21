#include "view_browser.h"

#include <stdexcept>
#include <string>
#include <utility>

void ViewBrowser::set_full_view(
    uint64_t row_count
)
{
    rows_.clear();
    row_count_ = row_count;
    full_view_ = true;
}


void ViewBrowser::set_rows(
    std::vector<uint32_t> rows
)
{
    rows_ = std::move(rows);
    row_count_ = rows_.size();
    full_view_ = false;
}


const ParsedEntry&
ViewBrowser::at(
    uint64_t logical_index
) const
{
    if (logical_index >= row_count_) {
        throw std::out_of_range(
            "ViewBrowser index out of range"
        );
    }

    if (full_view_) {
        return data_.read_entry_at(
            logical_index
        );
    }

    return data_.read_entry_at(
        rows_[logical_index]
    );
}


size_t
ViewBrowser::size() const noexcept
{
    return static_cast<size_t>(
        row_count_
    );
}