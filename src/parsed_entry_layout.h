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
    uint8_t  data_len;
    //uint8_t  changed;
    uint8_t  data[64];
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

static constexpr std::size_t kLogRecordSize = sizeof(LogRecord);
static constexpr std::size_t kParsedEntrySize = sizeof(ParsedEntry);