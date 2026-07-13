#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
#include <optional>
#include <string>
#include <vector>
#endif

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
	FMT_ASC        = 9,
	FMT_BLF        = 10,
};

#ifndef __cplusplus
#error can_parser.h requires a C++ compiler
#endif

struct ParsedEntry;
struct LogRecord;

#ifdef __cplusplus

// CP_EXPORT std::vector<ParsedEntry> parse_file(const std::string& path) __attribute__((deprecated("use parse_lines instead")));

// CP_EXPORT std::vector<ParsedEntry> parse_file_with_fmt(const std::string& path,
// 											 int32_t fmt) __attribute__((deprecated("use parse_lines instead")));

CP_EXPORT std::vector<LogRecord> parse_lines(const std::string& src);

CP_EXPORT std::optional<LogRecord> parse_line(const std::string& line);

#endif

/// @brief run_worker_segmented
/// NOTICE: The function is not responsible for stopping when catch the garbage entry. 
/// NOTICE: Valid entry will be displayed on the control screen to indicate the entry has been parse sucessfully. 
/// NOTICE: The application should provide stop button, the only way to to stop parsing is by killing the process while running.
/// NOTICE: However, the function will return immediately if any malfunction on writing database ?
/// @param file_path 
/// @param token_id 
/// @return None
/// BUG: 20260708    raise FileNotFoundError(f"No mmap files found for token: {token}")
///  FileNotFoundError: No mmap files found for token: 7a7bc1b6e1ed4e71a01296d542092bf0
/// This is consider to relative filepath
CP_EXPORT int32_t run_worker_segmented(const char* file_path,
											  const char* token_id);

CP_EXPORT uint32_t fs_core_abi_version();
