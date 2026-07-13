#pragma once

#include <cstddef>
#include <cstdint>

#pragma pack(push, 1)
// 20260629
/* NOTE: The mmap storage should contain the can log data only, not the context data */
// CANMessage           <-- runtime object
// LogLine          <-- domain UI object
// LogRecord    <-- mmap/disk layout
//
struct LogRecord {
    double   timestamp;
    uint32_t can_id;
    uint8_t  direction;
    uint8_t  data_len;
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

static constexpr uint32_t kInvalidRow = UINT32_MAX;
#pragma pack(push, 1)
// Parse and add the metadata for displaying on UI -> output for GUI
struct ParsedEntry : LogRecord {
    uint32_t line_number;
    double last_timestamp;
    uint8_t changed;
    uint32_t prev_same_can = kInvalidRow;
    uint32_t next_same_can = kInvalidRow;
};
#pragma pack(pop)




static constexpr std::size_t kParsedEntrySize = sizeof(ParsedEntry);
//static constexpr std::size_t kEntryUpdateSize = sizeof(EntryUpdate);

