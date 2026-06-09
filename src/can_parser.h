#pragma once

#include <cstdint>

#if defined(_WIN32)
#  define CP_EXPORT __declspec(dllexport)
#else
#  define CP_EXPORT __attribute__((visibility("default")))
#endif

enum ParserStatus : uint32_t {
	PARSER_STATUS_RUNNING = 0,
	PARSER_STATUS_DONE    = 1,
	PARSER_STATUS_ERROR   = 2,
};

enum FormatType : int {
	FMT_UNKNOWN    = 0,
	FMT_CANOE      = 1,
	FMT_CANOE_FULL = 2,
	FMT_CANOE_CMP  = 3,
	FMT_CANCMD     = 4,
	FMT_FILTER     = 5,
	FMT_CANSUKE    = 6,
	FMT_CANCMD_T2  = 7,
	FMT_CANCMD_T3  = 8,
};

struct ParsedEntry;

#ifdef __cplusplus
extern "C" {
#endif

CP_EXPORT int32_t can_parser_parse_file(const char* path,
										ParsedEntry** out_entries,
										uint32_t* out_count);

CP_EXPORT int32_t can_parser_parse_file_with_fmt(const char* path,
												 int32_t fmt,
												 ParsedEntry** out_entries,
												 uint32_t* out_count);

CP_EXPORT int32_t can_parser_parse_line(const char* line,
										uint32_t line_num,
										ParsedEntry* out);

CP_EXPORT void can_parser_free_entries(ParsedEntry* ptr);

CP_EXPORT int32_t can_parser_run_worker_segmented(const char* file_path,
											  const char* token_id,
											  FormatType fmt);

CP_EXPORT uint32_t fs_core_abi_version();

#ifdef __cplusplus
}
#endif
