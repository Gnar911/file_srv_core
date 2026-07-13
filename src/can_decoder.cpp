/*
 * can_decoder.cpp
 *
 * C++ hot-path for CAN signal decoding.
 *
 * DBC metadata is loaded ONCE via can_decoder_load_db() and stored in C++
 * heap memory.  The hot-path can_decoder_run() never touches the DB on disk —
 * it only reads data.mmap (parsed CAN entries) and writes (SoA layout):
 *   - signal_dir.mmap   (can_id, signal_id) → (index_offset, value_offset, rawvalue_offset, sample_count, signal_count)
 *   - row_index_changed.mmap uint32[] row index into data.mmap where signal raw value changed
 *   - row_index.mmap    uint32[]  row index into data.mmap per sample
 *   - value.mmap        float64[] physical values per sample
 *   - rawvalue.mmap     int64[]   raw decoded integers per sample
 *
 * Two-pass algorithm:
 *   Pass 1 — count samples per (message, signal) for exact offset computation.
 *   Pass 2 — extract raw values, compute physical values, write into 3 SoA arrays.
 *
 * Build: included in native_sdk_native shared library via CMakeLists.txt
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "can_analyzer_log.h"
#include "can_decoder.h"

#ifndef LOGGING_TRACE_ENABLED
#if defined(__LW_TRACE)
#define LOGGING_TRACE_ENABLED CBCM_INFO("TRACE %s", __FUNCTION__);
#else
#define LOGGING_TRACE_ENABLED
#endif
#endif

// ═══════════════════════════════════════════════════════════════════════════
// CAN signal bit extraction
// ═══════════════════════════════════════════════════════════════════════════


inline int64_t extract_signal_le(const uint8_t* data, int data_len,
                                         int start_bit, int bit_length) {
    uint64_t raw = 0;
    for (int i = 0; i < bit_length; i++) {
        int bp       = start_bit + i;
        int byte_idx = bp >> 3;
        int bit_idx  = bp & 7;
        if (byte_idx < data_len)
            raw |= (uint64_t)((data[byte_idx] >> bit_idx) & 1) << i;
    }
    return static_cast<int64_t>(raw);
}

inline int64_t extract_signal_be(const uint8_t* data, int data_len,
                                         int start_bit, int bit_length) {
    uint64_t raw = 0;
    int bp = start_bit;
    for (int i = bit_length - 1; i >= 0; i--) {
        int byte_idx   = bp >> 3;
        int bit_in_byte = bp & 7;
        if (byte_idx < data_len)
            raw |= (uint64_t)((data[byte_idx] >> bit_in_byte) & 1) << i;
        if ((bp & 7) == 0)
            bp += 15;
        else
            bp -= 1;
    }
    return static_cast<int64_t>(raw);
}

inline int64_t sign_extend(int64_t raw, int bit_length) {
    if (bit_length > 0 && bit_length < 64) {
        uint64_t sign_mask = 1ULL << (bit_length - 1);
        if (static_cast<uint64_t>(raw) & sign_mask) {
            raw |= ~static_cast<int64_t>((1ULL << bit_length) - 1);
        }
    }
    return raw;
}

int64_t extract_signal(const uint8_t* data, int data_len,
                       const SignalDef& sig) {
    int64_t raw;
    if (sig.byte_order == 0)
        raw = extract_signal_le(data, data_len, sig.start_bit, sig.bit_length);
    else
        raw = extract_signal_be(data, data_len, sig.start_bit, sig.bit_length);

    if (sig.is_signed)
        raw = sign_extend(raw, sig.bit_length);

    return raw;
}

int32_t CanDecoder::load_db(const CanDatabaseModel& model) {
    model_.messages = model.messages;
    model_.signals = model.signals;

    model_.canid_to_msg.clear();
    if (!model.canid_to_msg.empty()) {
        model_.canid_to_msg = model.canid_to_msg;
    } else {
        model_.canid_to_msg.reserve(model_.messages.size());
        for (uint32_t i = 0; i < model_.messages.size(); i++) {
            model_.canid_to_msg[model_.messages[i].can_id] = i;
        }
    }

    db_loaded_ = true;
    return 0;
}

void CanDecoder::free_db() {
    model_.messages.clear();
    model_.messages.shrink_to_fit();
    model_.signals.clear();
    model_.signals.shrink_to_fit();
    model_.canid_to_msg.clear();
    db_loaded_ = false;
}

std::vector<DecodedSignal> CanDecoder::decode_entry(uint32_t can_id,
                                                            const uint8_t* data,
                                                            uint8_t data_len,
                                                            uint32_t max_signals) const {
    std::vector<DecodedSignal> decoded;

    if (!db_loaded_) {
        return decoded;
    }

    auto it = model_.canid_to_msg.find(can_id);
    if (it == model_.canid_to_msg.end()) {
        return decoded;
    }

    const MessageDef& msg = model_.messages[it->second];
    const int dl = static_cast<int>(data_len);
    const uint32_t n = (max_signals == 0)
        ? static_cast<uint32_t>(msg.signal_count)
        : ((msg.signal_count < max_signals) ? msg.signal_count : max_signals);

    decoded.reserve(n);
    for (uint32_t si = 0; si < n; si++) {
        const SignalDef& sig = model_.signals[msg.signal_offset + si];
        const int64_t raw = extract_signal(data, dl, sig);
        const double phys = static_cast<double>(raw) * sig.scale + sig.offset;

        DecodedSignal out;
        out.signal_name = std::string("signal_") + std::to_string(si);
        out.raw_value = raw;
        out.phys_value = phys;
        decoded.push_back(out);
    }

    return decoded;
}

std::vector<DecodedSignal> CanDecoder::decode_entry(const LogRecord& entry,
                                                            uint32_t max_signals) const {
    return decode_entry(entry.can_id,
                        entry.data,
                        entry.data_len,
                        max_signals);
}

bool CanDecoder::is_loaded() const {
    return db_loaded_;
}

const std::vector<MessageDef>& CanDecoder::messages() const {
    return model_.messages;
}

const std::vector<SignalDef>& CanDecoder::signals() const {
    return model_.signals;
}

const std::unordered_map<uint32_t, uint32_t>& CanDecoder::canid_to_msg() const {
    return model_.canid_to_msg;
}

