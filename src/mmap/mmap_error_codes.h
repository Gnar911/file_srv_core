#pragma once

#include <cstdint>

namespace file_service {
namespace mmap {

namespace error_code {

enum class ErrorCode : int32_t {
	Ok = 0,

	InvalidReadArg = -201,
	MmapOpenFailed = -202,
	ReadOutOfRange = -203,

	HeaderSegment0Missing = -210,
	HeaderInvalid = -211,
	HeaderZeroMetadata = -212,
	SegmentIndexOutOfRange = -213,

	EmptySegmentPath = -220,
	ReadCountOpenFailed = -221,
	ReadCountHeaderTooSmall = -222,

	EmptyCapacityPath = -230,
	ReadCapacityOpenFailed = -231,
	ReadCapacityHeaderTooSmall = -232,

	WriterNotReady = -18,
	OpenInitFailed = -5,
	OpenNextSegmentFailed = -6,

	ReadRowsEmptyRequest = -1201,
	ReadRowsNoData = -1202,
	ReadRowsOutOfRange = -1203,
	ReadRowsEmptyResult = -1204,
};

constexpr int32_t to_i32(ErrorCode code) {
	return static_cast<int32_t>(code);
}

// Keep int32 aliases at API boundaries (C ABI / pybind / existing signatures).
inline constexpr int32_t kOk = to_i32(ErrorCode::Ok);

inline constexpr int32_t kInvalidReadArg = to_i32(ErrorCode::InvalidReadArg);
inline constexpr int32_t kMmapOpenFailed = to_i32(ErrorCode::MmapOpenFailed);
inline constexpr int32_t kReadOutOfRange = to_i32(ErrorCode::ReadOutOfRange);

inline constexpr int32_t kHeaderSegment0Missing = to_i32(ErrorCode::HeaderSegment0Missing);
inline constexpr int32_t kHeaderInvalid = to_i32(ErrorCode::HeaderInvalid);
inline constexpr int32_t kHeaderZeroMetadata = to_i32(ErrorCode::HeaderZeroMetadata);
inline constexpr int32_t kSegmentIndexOutOfRange = to_i32(ErrorCode::SegmentIndexOutOfRange);

inline constexpr int32_t kEmptySegmentPath = to_i32(ErrorCode::EmptySegmentPath);
inline constexpr int32_t kReadCountOpenFailed = to_i32(ErrorCode::ReadCountOpenFailed);
inline constexpr int32_t kReadCountHeaderTooSmall = to_i32(ErrorCode::ReadCountHeaderTooSmall);

inline constexpr int32_t kEmptyCapacityPath = to_i32(ErrorCode::EmptyCapacityPath);
inline constexpr int32_t kReadCapacityOpenFailed = to_i32(ErrorCode::ReadCapacityOpenFailed);
inline constexpr int32_t kReadCapacityHeaderTooSmall = to_i32(ErrorCode::ReadCapacityHeaderTooSmall);

inline constexpr int32_t kWriterNotReady = to_i32(ErrorCode::WriterNotReady);
inline constexpr int32_t kOpenInitFailed = to_i32(ErrorCode::OpenInitFailed);
inline constexpr int32_t kOpenNextSegmentFailed = to_i32(ErrorCode::OpenNextSegmentFailed);

inline constexpr int32_t kReadRowsEmptyRequest = to_i32(ErrorCode::ReadRowsEmptyRequest);
inline constexpr int32_t kReadRowsNoData = to_i32(ErrorCode::ReadRowsNoData);
inline constexpr int32_t kReadRowsOutOfRange = to_i32(ErrorCode::ReadRowsOutOfRange);
inline constexpr int32_t kReadRowsEmptyResult = to_i32(ErrorCode::ReadRowsEmptyResult);

} // namespace error_code

} // namespace mmap
} // namespace file_service
