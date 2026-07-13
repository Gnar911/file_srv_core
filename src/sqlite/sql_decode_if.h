//20260608 ~ 20260609
/*
Suppose:
VehicleSpeed is 12 bytes.

You have:
200 million decoded samples
Then:
200M × 12 bytes = 2.4 GB

Using:
uint16_t signal_id
you store: 2 bytes
instead of: 10-30 bytes per row.

Decide between SoA and Aos:
SoA: Too many rows but each row is smaller
struct DecodedSignal {
    uint32_t can_id;
    uint16_t signal_id;
    uint32_t row_index;
    int64_t raw_value;
    double phys_value;
    bool changed;
};

AoS: -> fewer rows but each row is larger
CREATE TABLE signal_data
(
    can_id INTEGER NOT NULL,
    signal_name TEXT NOT NULL,
    row_index_blob BLOB NOT NULL,
    raw_value_blob BLOB NOT NULL,
    phys_value_blob BLOB NOT NULL,
    changed_row_blob BLOB,
    PRIMARY KEY(can_id, signal_name)
);
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "can_decoder.h"
#include "sqlite/sqlite_wrapper_RAII.h"

// Data to write into db
struct DecodeProgress
{
    uint64_t processed_rows;
    uint64_t total_rows;
};

struct DecodedSignalChunk {
    uint32_t can_id;
    std::string signal_name;
    std::vector<uint32_t> row_index;
    std::vector<int64_t> raw_value;
    std::vector<double> phys_value;

    /// NOTE: Add prev and next pointer to detect changed
};

class DecodedSignalDatabase {
public:
    explicit DecodedSignalDatabase(
        const std::string& token_path);

    std::string db_path() const;

    int32_t begin_transaction();

    int32_t commit_transaction();

    int32_t write_signals(const std::vector<DecodedSignalChunk>& chunks);

    std::vector<std::string> get_signal_names(uint32_t can_id);

    DecodedSignalChunk get_signal_samples(uint32_t can_id, const std::string& signal_name);

private:
    std::string db_path_;
    Connection db_{};
    Statement upsert_stmt_{};
};