#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cstdio>
#include "mmap_wrapper_RAII.h"
#include "parsed_entry_layout.h"
#include <unordered_map>

/// NOTICE:
/*
token.header
+----------------------+
| MmapHeaderConstract  |
+----------------------+

token.000
+----------------------+
| Object[]             |
+----------------------+

token.001
+----------------------+
| Object[]             |
+----------------------+

token.002
+----------------------+
| Object[]             |
+----------------------+
*/


#if defined(_WIN32)
#  define FS_EXPORT __declspec(dllexport)
#else
#  define FS_EXPORT
#endif


constexpr std::size_t kMmapSourceFilePathCapacity = 512;

#pragma pack(push, 1)
struct MmapHeaderConstract {
    int32_t write_count;
    uint32_t capacity;
    uint32_t segment_count;
    uint8_t reserved[12];
    uint16_t source_file_path_len;
    char source_file_path[kMmapSourceFilePathCapacity];
};
#pragma pack(pop)

class HeaderView {
public:
    explicit HeaderView(const std::filesystem::path& file_path,
                        MMapAccess access = MMapAccess::ReadOnly);

    HeaderView(const HeaderView&) = delete;
    HeaderView& operator=(const HeaderView&) = delete;

    HeaderView(HeaderView&&) noexcept = default;
    HeaderView& operator=(HeaderView&&) noexcept = default;

    [[nodiscard]]
    int32_t write_count() const noexcept;

    [[nodiscard]]
    uint32_t segment_count() const noexcept;

    

    [[nodiscard]]
    uint32_t capacity() const noexcept;

    [[nodiscard]]
    std::string_view source_file_path() const noexcept;

    [[nodiscard]]
    bool valid() const noexcept;

    void set_write_count(int32_t value);
    void set_segment_count(uint32_t value);
    void set_source_file_path(std::string_view value);

private:
    [[nodiscard]]
    const MmapHeaderConstract& header() const noexcept;

    [[nodiscard]]
    MmapHeaderConstract& header_mut() noexcept;

private:
    MMapAccess access_ = MMapAccess::ReadOnly;
    MMapHandle mmap_;
};



template <typename TObject>
class SegmentView {
public:
    static_assert(std::is_trivially_copyable<TObject>::value,
                  "SegmentView<TObject> requires TObject to be trivially copyable");

    using ObjectType = TObject;

    // Number of objects (TObject) stored per segment.
    static constexpr uint32_t kObjectsPerSegment = 100'000;

    // Capacity expressed as number of objects (not bytes).
    static constexpr int32_t kDataSegmentCapacity = static_cast<int32_t>(kObjectsPerSegment);

    // static constexpr uint32_t kDataSegmentCapacity = 1'000'000;

    explicit SegmentView(std::filesystem::path base_path,
                         MMapAccess access = MMapAccess::ReadOnly)
        : base_path_(std::move(base_path)),
          access_(access)
    {
        // eagerly ensure segment 0 is available:
        if (access_ == MMapAccess::ReadWrite) {
            // create the first segment file and map it for write
            (void)open_or_create_segment_for_write(0);
        } else {
            // if a segment file 0 exists, open it to validate the mapping
            auto p = segment_path(0);
            if (std::filesystem::exists(p)) {
                (void)open_segment(0);
            }
        }
    }

    SegmentView(const SegmentView&) = delete;
    SegmentView& operator=(const SegmentView&) = delete;

    SegmentView(SegmentView&&) noexcept = default;
    SegmentView& operator=(SegmentView&&) noexcept = default;

    [[nodiscard]]
    const ObjectType& read_at(
        int32_t row_index)
    {
        const uint32_t seg_idx =
            static_cast<uint32_t>(row_index / static_cast<int32_t>(kDataSegmentCapacity));
        const uint32_t local_idx =
            static_cast<uint32_t>(row_index % static_cast<int32_t>(kDataSegmentCapacity));

        return record(seg_idx, local_idx);
    }

    [[nodiscard]]
    bool mapped(
        uint32_t segment_idx) const noexcept
    {
        return segments_.find(segment_idx) != segments_.end();
    }

    [[nodiscard]]
    int32_t write_next(const ObjectType& record)
    {
        if (access_ != MMapAccess::ReadWrite) {
            throw MMapError("SegmentView is read-only");
        }
        const int32_t cur_index = write_next_;
        const uint32_t seg_idx =
            static_cast<uint32_t>(cur_index / static_cast<int32_t>(kDataSegmentCapacity));
        const uint32_t local_idx =
            static_cast<uint32_t>(cur_index % static_cast<int32_t>(kDataSegmentCapacity));

        auto& mmap = open_or_create_segment_for_write(seg_idx);
        auto* rows = static_cast<ObjectType*>(mmap.data());
        rows[local_idx] = record;

        ++write_next_;
        row_index_ = cur_index;
        return cur_index;
    }



    void set_write_next(int32_t write_next) noexcept
    {
        write_next_ = write_next;
    }

    /// NOTE: Next index to write
    [[nodiscard]]
    int32_t next_index() const noexcept
    {
        return write_next_;
    }

    /// NOTE: Next index to write
    [[nodiscard]]
    int32_t cur_index() const noexcept
    {
        return row_index_;
    }

    void write_at(
        int32_t row_index,
        const ObjectType& record)
    {
        if (access_ != MMapAccess::ReadWrite) {
            throw MMapError("SegmentView is read-only");
        }

        if (row_index > write_next_) {
            throw MMapError("write_at row_index creates a gap");
        }

        const uint32_t seg_idx =
            static_cast<uint32_t>(row_index / static_cast<int32_t>(kDataSegmentCapacity));
        const uint32_t local_idx =
            static_cast<uint32_t>(row_index % static_cast<int32_t>(kDataSegmentCapacity));

        auto& mmap = open_or_create_segment_for_write(seg_idx);
        auto* rows = static_cast<ObjectType*>(mmap.data());
        rows[local_idx] = record;

        if (row_index == write_next_) {
            ++write_next_;
        }
    }

    void close_all()
    {
        segments_.clear();
    }

private:
    [[nodiscard]]
    const ObjectType& record(
        uint32_t segment_idx,
        uint32_t local_idx)
    {
        if (local_idx >= kDataSegmentCapacity) {
            throw MMapError("local_idx out of range");
        }

        return records(segment_idx)[local_idx];
    }

    [[nodiscard]]
    const ObjectType* records(
        uint32_t segment_idx)
    {
        auto& mmap = open_segment(segment_idx);

        return static_cast<const ObjectType*>(mmap.data());
    }

    [[nodiscard]]
    MMapHandle& open_segment(
        uint32_t segment_idx)
    {
        auto it = segments_.find(segment_idx);

        if (it != segments_.end()) {
            return it->second;
        }

        auto path = segment_path(segment_idx);

        auto [iter, inserted] =
            segments_.try_emplace(
                segment_idx,
                path,
                access_);

        return iter->second;
    }

    [[nodiscard]]
    std::filesystem::path segment_path(
        uint32_t segment_idx) const
    {
        char num[16];
        std::snprintf(num, sizeof(num), ".%03u", segment_idx);

        return base_path_.string() + num;
    }

    [[nodiscard]]
    MMapHandle& open_or_create_segment_for_write(
        uint32_t segment_idx)
    {
        if (access_ != MMapAccess::ReadWrite) {
            throw MMapError("SegmentView is read-only");
        }

        auto it = segments_.find(segment_idx);
        if (it != segments_.end()) {
            return it->second;
        }

        auto path = segment_path(segment_idx);

        if (std::filesystem::exists(path)) {
            auto [iter, inserted] =
                segments_.try_emplace(
                    segment_idx,
                    path,
                    access_);
            return iter->second;
        }

        // Bytes required for one segment: object count * sizeof(element)
        constexpr std::size_t kSegmentBytes =
            static_cast<std::size_t>(kDataSegmentCapacity) * sizeof(ObjectType);

        auto [iter, inserted] = segments_.try_emplace(segment_idx, path, kSegmentBytes);
        return iter->second;
    }

private:
    std::filesystem::path base_path_;
    MMapAccess access_ = MMapAccess::ReadOnly;
    int32_t write_next_ = 0;
    int32_t row_index_ = 0;

    std::unordered_map<uint32_t, MMapHandle> segments_;
};

using ParsedEntrySegmentView = SegmentView<ParsedEntry>;
using LogRecordSegmentView = SegmentView<LogRecord>;

