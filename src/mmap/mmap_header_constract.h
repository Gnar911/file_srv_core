#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#  define FS_EXPORT __declspec(dllexport)
#else
#  define FS_EXPORT
#endif

namespace file_service {

constexpr std::size_t kMmapSourceFilePathCapacity = 512;

#pragma pack(push, 1)
struct MmapHeaderConstract {
    uint64_t write_count;
    uint32_t capacity;
    uint32_t status;
    uint32_t segment_count;
    uint8_t reserved[12];
    uint16_t source_file_path_len;
    char source_file_path[kMmapSourceFilePathCapacity];
};
#pragma pack(pop)

constexpr std::size_t kMmapHeaderConstractSize = sizeof(MmapHeaderConstract);
constexpr std::size_t kMmapHeaderConstractWriteCountOffset = offsetof(MmapHeaderConstract, write_count);
constexpr std::size_t kMmapHeaderConstractCapacityOffset = offsetof(MmapHeaderConstract, capacity);
constexpr std::size_t kMmapHeaderConstractStatusOffset = offsetof(MmapHeaderConstract, status);
constexpr std::size_t kMmapHeaderConstractSegmentCountOffset = offsetof(MmapHeaderConstract, segment_count);

void init_mmap_header_constract(MmapHeaderConstract& header, uint32_t capacity, uint32_t status, uint32_t segment_count) noexcept;
void set_mmap_header_source_file_path(MmapHeaderConstract& header, const char* file_path) noexcept;

} // namespace file_service

extern "C" {

FS_EXPORT uint32_t mmap_header_constract_size();
FS_EXPORT uint32_t mmap_header_constract_write_count_offset();
FS_EXPORT uint32_t mmap_header_constract_capacity_offset();
FS_EXPORT uint32_t mmap_header_constract_status_offset();
FS_EXPORT uint32_t mmap_header_constract_segment_count_offset();

}
