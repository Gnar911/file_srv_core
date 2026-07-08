#include "mmap_decoder_value.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "can_decoder.h"
#include "mmap_error_codes.h"
#include "mmap_wrapper.h"

namespace file_service {
namespace mmap {
namespace {

std::string make_segment_path(const std::string& base_path, uint32_t seg_idx) {
    std::string stem = base_path;
    if (stem.size() >= 5 && stem.compare(stem.size() - 5, 5, ".mmap") == 0) {
        stem.resize(stem.size() - 5);
    }
    char num[16];
    std::snprintf(num, sizeof(num), ".%03u.mmap", seg_idx);
    return stem + num;
}

std::vector<std::string> discover_segments(const std::string& base_path) {
    std::vector<std::string> out;
    for (uint32_t i = 0; i < 10000; ++i) {
        const std::string path = make_segment_path(base_path, i);
        if (!mmap_file_exists(path.c_str())) {
            break;
        }
        out.push_back(path);
    }
    return out;
}

int32_t read_header(const std::string& path, SoAHeader& out_hdr) {
    MMapHandle h = {};
    mmap_open_ro(path.c_str(), h);
    if (h.addr == nullptr || h.size < sizeof(SoAHeader)) {
        mmap_close(h);
        return file_service::mmap::error_code::kHeaderInvalid;
    }
    std::memcpy(&out_hdr, h.addr, sizeof(SoAHeader));
    mmap_close(h);
    return 0;
}

int32_t read_f64(const std::vector<std::string>& paths,
                uint64_t global_offset,
                double& out_value) {
    if (paths.empty()) {
        return file_service::mmap::error_code::kHeaderSegment0Missing;
    }

    SoAHeader hdr0 = {};
    int32_t rc = read_header(paths.front(), hdr0);
    if (rc != 0 || hdr0.capacity == 0) {
        return (rc != 0) ? rc : file_service::mmap::error_code::kHeaderZeroMetadata;
    }

    const uint64_t seg_idx = global_offset / static_cast<uint64_t>(hdr0.capacity);
    const uint64_t local_idx = global_offset % static_cast<uint64_t>(hdr0.capacity);
    if (seg_idx >= paths.size()) {
        return file_service::mmap::error_code::kReadOutOfRange;
    }

    MMapHandle h = {};
    mmap_open_ro(paths[static_cast<size_t>(seg_idx)].c_str(), h);

    if (h.addr == nullptr || h.size < sizeof(SoAHeader)) {
        mmap_close(h);
        return file_service::mmap::error_code::kHeaderInvalid;
    }

    const auto* hdr = reinterpret_cast<const SoAHeader*>(h.addr);
    if (local_idx >= hdr->capacity) {
        mmap_close(h);
        return file_service::mmap::error_code::kReadOutOfRange;
    }

    const size_t off = sizeof(SoAHeader) + static_cast<size_t>(local_idx) * sizeof(double);
    if (off + sizeof(double) > h.size) {
        mmap_close(h);
        return file_service::mmap::error_code::kReadOutOfRange;
    }

    std::memcpy(&out_value, reinterpret_cast<const uint8_t*>(h.addr) + off, sizeof(double));
    mmap_close(h);
    return 0;
}

std::pair<uint64_t, uint64_t> to_page_window(int64_t first, int64_t last) {
    const uint64_t start = first > 0 ? static_cast<uint64_t>(first) : 0ULL;
    if (last < first) {
        return {start, 0};
    }
    return {start, static_cast<uint64_t>((last - first) + 1)};
}

} // namespace

DecoderValueMmap::DecoderValueMmap(std::string base_path,
                                   uint32_t segment_capacity)
    : base_path_(std::move(base_path)),
      segment_capacity_(segment_capacity) {}

std::vector<std::string> DecoderValueMmap::planned_segment_paths_() const {
    std::vector<std::string> out;
    for (uint32_t i = 0; i < segments_.size(); ++i) {
        out.push_back(make_segment_path(base_path_, i));
    }
    return out;
}

int32_t DecoderValueMmap::open_segment_(uint32_t seg_idx) {
    Segment seg;
    const std::string path = make_segment_path(base_path_, seg_idx);
    const size_t size = sizeof(SoAHeader) + static_cast<size_t>(segment_capacity_) * sizeof(double);
    mmap_create_rw(path.c_str(), size, seg.handle);
    seg.header = reinterpret_cast<SoAHeader*>(seg.handle.addr);
    seg.values = reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(seg.handle.addr) + sizeof(SoAHeader));
    seg.header->sample_count = 0;
    seg.header->capacity = segment_capacity_;
    segments_.push_back(std::move(seg));
    return 0;
}

int32_t DecoderValueMmap::ensure_segment_(uint64_t global_offset) {
    if (segment_capacity_ == 0) {
        return file_service::mmap::error_code::kHeaderZeroMetadata;
    }
    const uint64_t required_seg_idx = global_offset / segment_capacity_;
    if (required_seg_idx > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        return file_service::mmap::error_code::kReadOutOfRange;
    }
    while (segments_.size() <= required_seg_idx) {
        const int32_t rc = open_segment_(static_cast<uint32_t>(segments_.size()));
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

int32_t DecoderValueMmap::open_and_init() {
    close_and_finalize();
    if (segment_capacity_ == 0) {
        return file_service::mmap::error_code::kHeaderZeroMetadata;
    }
    return open_segment_(0);
}

int32_t DecoderValueMmap::write_at(uint64_t global_offset, double value) {
    const int32_t ensure_rc = ensure_segment_(global_offset);
    if (ensure_rc != 0) {
        return ensure_rc;
    }
    const uint64_t seg_idx = global_offset / segment_capacity_;
    const uint64_t local_idx = global_offset % segment_capacity_;
    segments_[static_cast<size_t>(seg_idx)].values[static_cast<size_t>(local_idx)] = value;
    return 0;
}

void DecoderValueMmap::publish_progress(uint64_t written_total, bool done) {
    (void)done;
    for (uint32_t seg_idx = 0; seg_idx < segments_.size(); ++seg_idx) {
        const uint64_t seg_start = static_cast<uint64_t>(seg_idx) * segment_capacity_;
        uint64_t seg_written = 0;
        if (written_total > seg_start) {
            seg_written = written_total - seg_start;
            if (seg_written > segment_capacity_) seg_written = segment_capacity_;
        }
        Segment& seg = segments_[seg_idx];
        seg.header->sample_count = seg_written;
    }
}

void DecoderValueMmap::close_and_finalize() {
    for (auto& seg : segments_) {
        mmap_close(seg.handle);
        seg.header = nullptr;
        seg.values = nullptr;
    }
    segments_.clear();
}

std::vector<std::string> DecoderValueMmap::segment_paths() const {
    if (!segments_.empty()) {
        return planned_segment_paths_();
    }
    return discover_segments(base_path_);
}

int32_t DecoderValueMmap::read_total_count(uint64_t& out_total_count) const {
    out_total_count = 0;
    const auto paths = segment_paths();
    if (paths.empty()) {
        return file_service::mmap::error_code::kHeaderSegment0Missing;
    }

    for (const auto& path : paths) {
        SoAHeader hdr = {};
        const int32_t rc = read_header(path, hdr);
        if (rc != 0) {
            return rc;
        }
        out_total_count += hdr.sample_count;
    }
    return 0;
}

int32_t DecoderValueMmap::read_value(uint64_t global_offset, double& out_value) const {
    const auto paths = segment_paths();
    return read_f64(paths, global_offset, out_value);
}

std::vector<double> DecoderValueMmap::read_page(int64_t first, int64_t last) const {
    const auto [start, count] = to_page_window(first, last);
    std::vector<double> out;
    out.reserve(static_cast<size_t>(count));

    for (uint64_t i = 0; i < count; ++i) {
        double value = 0.0;
        const int32_t rc = read_value(start + i, value);
        if (rc != 0) {
            break;
        }
        out.push_back(value);
    }

    return out;
}

} // namespace mmap
} // namespace file_service
