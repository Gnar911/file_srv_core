#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "parsed_entry_layout.h"

#if defined(_WIN32)
#  define CD_EXPORT __declspec(dllexport)
#else
#  define CD_EXPORT
#endif

#pragma pack(push, 1)

struct MessageDef {
    uint32_t can_id;
    uint16_t signal_count;
    uint16_t msg_length;
    uint32_t signal_offset;
    uint32_t padding;
};

struct SignalDef {
    uint16_t start_bit;
    uint16_t bit_length;
    uint8_t  byte_order;
    uint8_t  is_signed;
    uint8_t  has_choices;
    uint8_t  padding1;
    double   scale;
    double   offset;
    uint8_t  padding2[8];
};


#pragma pack(pop)

struct DecodedSignal {
    std::string signal_name;
    int64_t raw_value = 0;
    double phys_value = 0.0;
};

struct CanDatabaseModel {
    std::vector<MessageDef> messages;
    std::vector<SignalDef> signals;
    std::unordered_map<uint32_t, uint32_t> canid_to_msg;
};

class CanDecoder {
public:
    int32_t load_db(const CanDatabaseModel& model);
    void free_db();
    std::vector<DecodedSignal> decode_entry(const LogRecord& entry,
                                            uint32_t max_signals = 0) const;

    bool is_loaded() const;
    const std::vector<MessageDef>& messages() const;
    const std::vector<SignalDef>& signals() const;
    const std::unordered_map<uint32_t, uint32_t>& canid_to_msg() const;

private:
    std::vector<DecodedSignal> decode_entry(uint32_t can_id,
                                            const uint8_t* data,
                                            uint8_t data_len,
                                            uint32_t max_signals = 0) const;
                                            
    CanDatabaseModel model_;
    bool db_loaded_ = false;
};

int64_t extract_signal(const uint8_t* data, int data_len, const SignalDef& sig);

