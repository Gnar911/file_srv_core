#include "mmap_layout_view.h"

#include <cstring>
#include <cstdio>


HeaderView::HeaderView(const std::filesystem::path& file_path,
                       MMapAccess access)
    : access_(access),
      mmap_((access == MMapAccess::ReadWrite && !std::filesystem::exists(file_path))
                ? MMapHandle(file_path, sizeof(MmapHeaderConstract))
                : MMapHandle(file_path, access))
{
    if (mmap_.bytes() < sizeof(MmapHeaderConstract)) {
        throw MMapError("Header mmap too small: " + file_path.string());
    }

    // if capacity is zero (likely newly created/zeroed header), initialize it
    if (access == MMapAccess::ReadWrite && header_mut().capacity == 0) {
        header_mut().capacity = ParsedEntrySegmentView::kDataSegmentCapacity;
    }
}

const MmapHeaderConstract&
HeaderView::header() const noexcept
{
    return *static_cast<const MmapHeaderConstract*>(mmap_.data());
}

MmapHeaderConstract&
HeaderView::header_mut() noexcept
{
    return *static_cast<MmapHeaderConstract*>(mmap_.data());
}

bool HeaderView::valid() const noexcept
{
    return mmap_.is_open();
}

int32_t HeaderView::write_count() const noexcept
{
    return header().write_count;
}

uint32_t HeaderView::segment_count() const noexcept
{
    return header().segment_count;
}

uint32_t HeaderView::capacity() const noexcept
{
    return header().capacity;
}

std::string_view HeaderView::source_file_path() const noexcept
{
    return std::string_view(
        header().source_file_path,
        header().source_file_path_len);
}

void HeaderView::set_write_count(int32_t value)
{
    if (access_ != MMapAccess::ReadWrite) {
        throw MMapError("HeaderView is read-only");
    }
    header_mut().write_count = value;
}

void HeaderView::set_segment_count(uint32_t value)
{
    if (access_ != MMapAccess::ReadWrite) {
        throw MMapError("HeaderView is read-only");
    }
    header_mut().segment_count = value;
}


void HeaderView::set_source_file_path(std::string_view value)
{
    if (access_ != MMapAccess::ReadWrite) {
        throw MMapError("HeaderView is read-only");
    }

    auto& h = header_mut();
    const std::size_t max_len = sizeof(h.source_file_path);
    const std::size_t n = value.size() < max_len ? value.size() : max_len;
    std::memset(h.source_file_path, 0, max_len);
    if (n > 0) {
        std::memcpy(h.source_file_path, value.data(), n);
    }
    h.source_file_path_len = static_cast<uint16_t>(n);
}
