/*
 * can_parser.cpp
 * 2.
 * token_id is the base path for the segmented data family.
 * Native derives the index family base path from this token and writes the
 * segmented family mmaps directly.
 * Build: included in native_sdk_native shared library via CMakeLists.txt
 */
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#endif

#if !defined(_WIN32)
#include <sys/eventfd.h>
#include <unistd.h>
#endif

#include "mmap/mmap_wrapper_RAII.h"
#include "can_analyzer_log.h"
#include "can_parser.h"
// #include "prs_token.h"
// #include "mmap/mmap_header_constract.h"
//#include "mmap/index_mmap_layout.h"
#include "parsed_entry_layout.h"
#include "metadata_storage_if.h"
#include "asc_reader.h"

#ifndef LOGGING_TRACE_ENABLED
#if defined(__LW_TRACE)
#define LOGGING_TRACE_ENABLED __LW_TRACE()
#else
#define LOGGING_TRACE_ENABLED ;
#endif
#endif

static constexpr uint32_t kLastTimestampTableSize = 0x2000;
static constexpr int kNumThreads = 4;

struct LastTimestampTable {
    std::array<double, kLastTimestampTableSize> last{};
    std::array<uint8_t, kLastTimestampTableSize> seen{};

    inline double update_and_get_prev(uint32_t can_id, double ts) {
        if (can_id >= kLastTimestampTableSize) return ts;
        const double prev = seen[can_id] ? last[can_id] : ts;
        last[can_id] = ts;
        seen[can_id] = 1;
        return prev;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic array helper
// ─────────────────────────────────────────────────────────────────────────────
struct EntryBuf {
    ParsedEntry* data  = nullptr;
    uint32_t     size  = 0;
    uint32_t     cap   = 0;

    bool push(const ParsedEntry& e) {
        if (size == cap) {
            uint32_t new_cap = cap == 0 ? 4096 : cap * 2;
            ParsedEntry* p = (ParsedEntry*)realloc(data, new_cap * sizeof(ParsedEntry));
            if (!p) return false;
            data = p; cap = new_cap;
        }
        data[size++] = e;
        return true;
    }

    ParsedEntry* release_to_heap() {
        // Shrink to exact size (best-effort)
        if (size == 0) return nullptr;
        ParsedEntry* p = (ParsedEntry*)realloc(data, size * sizeof(ParsedEntry));
        data = nullptr; cap = 0; size = 0;
        return p;
    }

    ~EntryBuf() { free(data); }
};

struct ThreadOut {
    EntryBuf buf;
    uint64_t parsed = 0;
};


// Parse with a known format (skip detection; faster hot path)
// static bool parse_with_fmt(const char* line, size_t len,
//                             FormatType fmt, uint32_t line_num,
//                             ParsedEntry& e) {
        
// }

// static void parse_input_parallel(const char* src,
//                                  const char* end,
//                                  FormatType detected,
//                                  std::array<ThreadOut, kNumThreads>& tout) {
//     struct ByteRange { const char* begin; const char* end; };
//     ByteRange ranges[kNumThreads];
//     {
//         const char* prev_end = src;
//         const size_t input_size = static_cast<size_t>(end - src);
//         const size_t chunk_bytes = input_size / static_cast<size_t>(kNumThreads);
//         for (int i = 0; i < kNumThreads; i++) {
//             ranges[i].begin = prev_end;
//             if (i == kNumThreads - 1 || prev_end >= end) {
//                 ranges[i].end = end;
//                 for (int j = i + 1; j < kNumThreads; j++) {
//                     ranges[j].begin = end;
//                     ranges[j].end = end;
//                 }
//                 break;
//             }
//             const char* nominal = src + static_cast<size_t>(i + 1) * chunk_bytes;
//             if (nominal >= end) nominal = end - 1;
//             const char* nl = reinterpret_cast<const char*>(
//                 memchr(nominal, '\n', static_cast<size_t>(end - nominal)));
//             ranges[i].end = nl ? nl + 1 : end;
//             prev_end = ranges[i].end;
//         }
//     }

//     uint32_t chunk_newline_count[kNumThreads] = {};
//     {
//         std::vector<std::thread> count_threads;
//         count_threads.reserve(kNumThreads);
//         for (int i = 0; i < kNumThreads; i++) {
//             count_threads.emplace_back([&, i]() {
//                 uint32_t cnt = 0;
//                 const char* p = ranges[i].begin;
//                 const char* e = ranges[i].end;
//                 while (p < e) {
//                     const char* nl = reinterpret_cast<const char*>(
//                         memchr(p, '\n', static_cast<size_t>(e - p)));
//                     if (!nl) break;
//                     ++cnt;
//                     p = nl + 1;
//                 }
//                 chunk_newline_count[i] = cnt;
//             });
//         }
//         for (auto& th : count_threads) th.join();
//     }

//     uint32_t chunk_start_line[kNumThreads];
//     chunk_start_line[0] = 0;
//     for (int i = 1; i < kNumThreads; i++) {
//         chunk_start_line[i] = chunk_start_line[i - 1] + chunk_newline_count[i - 1];
//     }

//     std::vector<std::thread> threads;
//     threads.reserve(kNumThreads);
//     for (int t = 0; t < kNumThreads; t++) {
//         threads.emplace_back([&, t]() {
//             const char* cur = ranges[t].begin;
//             const char* chunk_end = ranges[t].end;
//             uint32_t lnum = chunk_start_line[t];
//             FormatType local_fmt = detected;

//             while (cur < chunk_end) {
//                 const char* eol = reinterpret_cast<const char*>(
//                     memchr(cur, '\n', static_cast<size_t>(chunk_end - cur)));
//                 const char* line_end = eol ? eol : chunk_end;
//                 size_t len = static_cast<size_t>(line_end - cur);
//                 while (len > 0 && cur[len - 1] == '\r') --len;
//                 ++lnum;

//                 ParsedEntry e;
//                     bool ok = parse_with_fmt(cur, len, local_fmt, lnum, e);

//                 if (ok) {
//                     if (!tout[t].buf.push(e)) {
//                         break;
//                     }
//                     ++tout[t].parsed;
//                 }

//                 cur = eol ? eol + 1 : chunk_end;
//             }
//         });
//     }
//     for (auto& th : threads) th.join();
// }

static std::vector<LogRecord> collect_entries_from_threads(
    const std::array<ThreadOut, kNumThreads>& tout) {
    size_t total = 0;
    for (int t = 0; t < kNumThreads; ++t) {
        total += tout[t].buf.size;
    }

    std::vector<LogRecord> entries;
    entries.reserve(total);
    for (int t = 0; t < kNumThreads; ++t) {
        for (uint32_t i = 0; i < tout[t].buf.size; ++i) {
            entries.push_back(tout[t].buf.data[i]);
        }
    }
    return entries;
}

/*
 * can_parser_parse_file
 *   Parses an entire CAN log text file.
 *   Detects format from the first valid line, then applies that parser to all
 *   subsequent lines.
 *
 *   path        : UTF-8 file path
 *   out_entries : receives pointer to malloc'd ParsedEntry array (caller frees
 *                 with can_parser_free_entries)
 *   out_count   : receives number of entries
 *   Returns 0 on success, negative on error.
 */
// CP_EXPORT std::vector<ParsedEntry> parse_file(const std::string& path) {
// }

/*
 * can_parser_parse_file_with_fmt
 *   Like can_parser_parse_file, but skips auto-detection.
 *   Python detects the format via regex, then passes the FormatType int here
 *   so C++ uses parse_with_fmt for every line (pure hot path, zero detection).
 *
 *   fmt         : FormatType integer (FMT_CANOE..FMT_CANCMD_T3)
 *   path        : UTF-8 file path
 *   out_entries : receives pointer to malloc'd ParsedEntry array (caller frees)
 *   out_count   : receives number of entries
 *   Returns 0 on success, negative on error.
 */
// CP_EXPORT std::vector<ParsedEntry> parse_file_with_fmt(const std::string& path,
//                                                   int32_t fmt) {
//     if (fmt < FMT_CANOE || fmt > FMT_CANCMD_T3) {
//         throw std::runtime_error("parse_file_with_fmt: invalid fmt");
//     }

//     FILE* f = fopen(path.c_str(), "rb");
//     if (!f) {
//         throw std::runtime_error("parse_file_with_fmt: failed to open file");
//     }

//     EntryBuf buf;
//     FormatType format = static_cast<FormatType>(fmt);
//     LastTimestampTable last_timestamp_by_id;

//     char      line[16384];
//     uint32_t  line_num = 0;
//     ParsedEntry e;

//     while (fgets(line, sizeof(line), f)) {
//         ++line_num;
//         size_t len = strlen(line);
//         while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) --len;
//         line[len] = '\0';

//         if (parse_with_fmt(line, len, format, line_num, e)) {
//             e.last_timestamp = last_timestamp_by_id.update_and_get_prev(e.can_id, e.timestamp);
//             buf.push(e);
//         }
//     }
//     fclose(f);

//     std::vector<ParsedEntry> out;
//     out.reserve(buf.size);
//     for (uint32_t i = 0; i < buf.size; ++i) {
//         out.push_back(buf.data[i]);
//     }
//     return out;
// }

/*
 * can_parser_parse_line
 *   Parse a single line (trying all formats). Useful for CSV/Excel per-row calls.
 *   Returns 1 on success, 0 on failure.
 */
CP_EXPORT std::optional<LogRecord> parse_line(const std::string& line) {
    std::string s(line);
    size_t len = s.size();
    while (len > 0 && (unsigned char)s[len-1] <= ' ') --len;
    s.resize(len);

    std::istringstream iss(s);
    ASCReader reader(iss, "hex", true);
    LogRecord tmp{};
    if (reader.next(tmp)) {
        return tmp;
    }
    return std::nullopt;
}

CP_EXPORT std::vector<LogRecord> parse_lines(const std::string& src) {
    std::string s(src);
    std::istringstream iss(s);
    ASCReader reader(iss, "hex", true);

    std::vector<LogRecord> parsed;
    LogRecord rec{};

    while (reader.next(rec)) {
        LogRecord e{};
        // copy LogRecord fields
        e.timestamp = rec.timestamp;
        e.can_id = rec.can_id;
        e.direction = rec.direction;
        e.data_len = rec.data_len;
        for (size_t i = 0; i < sizeof(e.data); ++i) e.data[i] = rec.data[i];
        e.is_extended_id = rec.is_extended_id;
        e.is_error_frame = rec.is_error_frame;
        e.is_remote_frame = rec.is_remote_frame;
        e.bitrate_switch = rec.bitrate_switch;
        e.error_state_indicator = rec.error_state_indicator;
        e.is_fd = rec.is_fd;
        std::strncpy(e.channel, rec.channel, sizeof(e.channel));
        parsed.push_back(e);
    }

    return parsed;
}

CP_EXPORT uint32_t fs_core_abi_version() {
    return 8;
}


CP_EXPORT int32_t run_worker_segmented(const char* file_path,
                                                  const char* token_id) {
    LOGGING_TRACE_ENABLED;
    if (!file_path || file_path[0] == '\0' || !token_id || token_id[0] == '\0') {
        return -1;
    }

    // Sequential ASC reader path using ASCReader
    std::vector<LogRecord> parsed_entries;

    // Try to open file as text stream for ASCReader
    {
        std::ifstream ifs(file_path);
        if (!ifs) {
            return -2;
        }

        ASCReader reader(ifs, "hex", true);
        LogRecord rec{};
        while (reader.next(rec)) {
            parsed_entries.push_back(rec);
        }
    }
    
    try
    {
        MetaDataStorageInterface handler{std::string(token_id)};
        handler.set_file_path(file_path);
        handler.write_entries(parsed_entries);
    }
    catch (const std::exception&) {
			return -1;
		}
    return 0;
}

