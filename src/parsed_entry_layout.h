#pragma once

#include <cstddef>
#include <cstdint>

#include "can_parser.h"

#pragma pack(push, 1)
// 20260629
/* NOTE: The mmap storage should contain the can log data only, not the context data */
// CANMessage           <-- runtime object
// LogLine          <-- domain UI object
// LogRecord    <-- mmap/disk layout
//
struct LogRecord {
    //uint32_t line_number;
    double   timestamp;
    //double   last_timestamp;
    uint32_t can_id;
    uint8_t  direction;
    //uint8_t  dlc;
    uint8_t  data_len;
    //uint8_t  changed;
    uint8_t  data[64];

	bool is_extended_id = false;
	bool is_error_frame = false;
	bool is_remote_frame = false;
	bool bitrate_switch = false;
	bool error_state_indicator = false;
    bool is_fd = false;

    char     channel[16];
};
#pragma pack(pop)


#pragma pack(push, 1)
// Parse and add the
struct ParsedEntry : LogRecord {
    uint32_t line_number;
    double last_timestamp;
    uint8_t changed;
};
#pragma pack(pop)

#pragma pack(push, 1)
// Update request DTO: trusted key (row_index, 0-based) + payload (LogRecord).
// Derived fields such as changed/last_timestamp are recomputed server-side.
struct EntryUpdate {
    uint64_t row_index;    // 0-based
    LogRecord record;
};
#pragma pack(pop)

static constexpr std::size_t kLogRecordSize = sizeof(LogRecord);
static constexpr std::size_t kParsedEntrySize = sizeof(ParsedEntry);
static constexpr std::size_t kEntryUpdateSize = sizeof(EntryUpdate);