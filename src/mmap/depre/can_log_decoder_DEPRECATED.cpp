
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "can_analyzer_log.h"
#include "can_decoder.h"
#include "can_log_decoder.h"
#include "decoded_mmap_if.h"
#include "mmap/mmap_wrapper.h"
#include "parsed_mmap_if.h"

#ifndef LOGGING_TRACE_ENABLED
#if defined(__LW_TRACE)
#define LOGGING_TRACE_ENABLED CBCM_INFO("TRACE %s", __FUNCTION__);
#else
#define LOGGING_TRACE_ENABLED
#endif
#endif

namespace {

struct SignalStats {
    uint32_t sample_count = 0;
    uint32_t changed_count = 0;

    bool has_previous = false;
    int64_t previous_raw = 0;
};

} // namespace

int32_t can_decoder_run(file_service::ParsedMmapInterface& parsed_mmap) {
    LOGGING_TRACE_ENABLED;

    const CanDecoderDatabase& db = CanDecoderDatabase::instance();
    if (!db.is_loaded()) {
        CBCM_ERROR("can_decoder_run: DB not loaded — call can_decoder_load_db first");
        return -3;
    }

    const std::vector<MessageDef>& messages = db.messages();
    const std::vector<SignalDef>& signals = db.signals();
    const std::unordered_map<uint32_t, uint32_t>& canid_to_msg = db.canid_to_msg();

    const MessageDef* msg_table = messages.data();
    const SignalDef*  sig_pool  = signals.data();
    const uint32_t    msg_count = static_cast<uint32_t>(messages.size());
    const uint32_t    total_sigs = static_cast<uint32_t>(signals.size());

    const uint64_t entry_count = parsed_mmap.get_total_entries_num();
    if (entry_count == 0) {
        CBCM_ERROR("No parsed mmap data found (last_error=%d)", parsed_mmap.last_error_code());
        return -2;
    }

    constexpr uint64_t kReadChunkSize = 100'000;

    CBCM_INFO("Decoder: %llu parsed entries to decode via ParsedMmapInterface",
              static_cast<unsigned long long>(entry_count));

    std::vector<SignalStats> stats(total_sigs);
    for (uint64_t start = 0; start < entry_count; start += kReadChunkSize) {
        const uint64_t end_exclusive = std::min<uint64_t>(start + kReadChunkSize, entry_count);
        const std::vector<ParsedEntry> page = parsed_mmap.read_page(
            static_cast<int64_t>(start),
            static_cast<int64_t>(end_exclusive - 1));
        if (page.size() != static_cast<size_t>(end_exclusive - start)) {
            CBCM_ERROR("Failed to read parsed mmap rows [%llu, %llu] (last_error=%d)",
                       static_cast<unsigned long long>(start),
                       static_cast<unsigned long long>(end_exclusive - 1),
                       parsed_mmap.last_error_code());
            return -2;
        }

        for (const ParsedEntry& entry : page) {
            auto it = canid_to_msg.find(entry.can_id);
            if (it == canid_to_msg.end()) continue;
            const MessageDef& msg = msg_table[it->second];
            const int dl = static_cast<int>(entry.data_len);
            for (uint16_t si = 0; si < msg.signal_count; si++) {
                const uint32_t flat = msg.signal_offset + si;
                auto& s = stats[flat];
                s.sample_count++;

                const SignalDef& sig = sig_pool[flat];
                const int64_t raw = extract_signal(entry.data, dl, sig);
                if (s.has_previous) {
                    if (raw != s.previous_raw && s.changed_count < std::numeric_limits<uint32_t>::max()) {
                        s.changed_count++;
                    }
                }
                s.previous_raw = raw;
                s.has_previous = true;
            }
        }
    }

    std::vector<uint64_t> offsets(total_sigs, 0);
    std::vector<uint64_t> changed_offsets(total_sigs, 0);
    uint32_t dir_count = 0;
    uint64_t sample_total = 0;
    uint64_t changed_sample_total = 0;
    for (uint32_t mi = 0; mi < msg_count; mi++) {
        const MessageDef& msg = msg_table[mi];
        for (uint16_t si = 0; si < msg.signal_count; si++) {
            const uint32_t flat = msg.signal_offset + si;
            const SignalStats& s = stats[flat];
            const uint32_t cnt = s.sample_count;
            const uint32_t changed_cnt = s.changed_count;
            if (cnt == 0) continue;
            offsets[flat] = sample_total;
            changed_offsets[flat] = changed_sample_total;
            sample_total += cnt;
            changed_sample_total += changed_cnt;
            dir_count++;
        }
    }

    CBCM_INFO("Decoder: %u directory entries, %llu total samples, %llu changed samples to write",
              dir_count,
              static_cast<unsigned long long>(sample_total),
              static_cast<unsigned long long>(changed_sample_total));

    file_service::DecodedMmapWriteContext decoded_writer(parsed_mmap.token_path());
    const int32_t writer_open_rc = decoded_writer.open_and_init();
    if (writer_open_rc != 0) {
        decoded_writer.close();
        return writer_open_rc;
    }

    uint32_t dir_out_idx = 0;
    uint64_t sample_cursor = 0;
    uint64_t changed_sample_cursor = 0;
    for (uint32_t mi = 0; mi < msg_count; mi++) {
        const MessageDef& msg = msg_table[mi];
        for (uint16_t si = 0; si < msg.signal_count; si++) {
            const uint32_t flat = msg.signal_offset + si;
            const SignalStats& s = stats[flat];
            const uint32_t cnt = s.sample_count;
            const uint32_t changed_cnt = s.changed_count;
            if (cnt == 0) continue;

            SignalDirectoryEntry de = {};
            de.can_id = msg.can_id;
            de.signal_id = si;
            de.padding = 0;
            de.index_offset = sample_cursor;
            de.value_offset = sample_cursor;
            de.rawvalue_offset = sample_cursor;
            de.changed_index_offset = changed_sample_cursor;
            de.sample_count = cnt;
            de.changed_sample_count = changed_cnt;
            de.signal_count = msg.signal_count;
            de.padding2 = 0;

            if (decoded_writer.write_directory_entry(dir_out_idx, de) != 0) {
                decoded_writer.close();
                return -4;
            }

            dir_out_idx++;
            sample_cursor += cnt;
            changed_sample_cursor += changed_cnt;
        }
    }

    std::vector<uint64_t> sample_write_offsets = offsets;

    auto write_sample = [&](uint64_t pos, uint32_t row_idx, double phys, int64_t raw) {
        (void)decoded_writer.write_sample(pos, row_idx, phys, raw);
    };

    auto write_changed_sample = [&](uint64_t pos, uint32_t row_idx) {
        (void)decoded_writer.write_changed_row_index(pos, row_idx);
    };

    std::vector<uint64_t> changed_write_offsets = changed_offsets;
    std::vector<uint8_t> has_prev_changed(total_sigs, 0);
    std::vector<int64_t> prev_raw_changed(total_sigs, 0);

    for (uint64_t start = 0; start < entry_count; start += kReadChunkSize) {
        const uint64_t end_exclusive = std::min<uint64_t>(start + kReadChunkSize, entry_count);
        const std::vector<ParsedEntry> page = parsed_mmap.read_page(
            static_cast<int64_t>(start),
            static_cast<int64_t>(end_exclusive - 1));
        if (page.size() != static_cast<size_t>(end_exclusive - start)) {
            decoded_writer.close();
            CBCM_ERROR("Failed to read parsed mmap rows [%llu, %llu] during write pass (last_error=%d)",
                       static_cast<unsigned long long>(start),
                       static_cast<unsigned long long>(end_exclusive - 1),
                       parsed_mmap.last_error_code());
            return -2;
        }

        for (size_t idx = 0; idx < page.size(); ++idx) {
            const uint64_t global_row = start + static_cast<uint64_t>(idx);
            const ParsedEntry& entry = page[idx];
            auto it = canid_to_msg.find(entry.can_id);
            if (it == canid_to_msg.end()) continue;
            const MessageDef& msg = msg_table[it->second];
            const int dl = static_cast<int>(entry.data_len);
            for (uint16_t si = 0; si < msg.signal_count; si++) {
                const uint32_t flat = msg.signal_offset + si;
                const SignalDef& sig = sig_pool[flat];
                const int64_t raw = extract_signal(entry.data, dl, sig);
                const double phys = static_cast<double>(raw) * sig.scale + sig.offset;
                const uint64_t sample_pos = sample_write_offsets[flat]++;
                write_sample(sample_pos, static_cast<uint32_t>(global_row), phys, raw);

                if (has_prev_changed[flat]) {
                    if (raw != prev_raw_changed[flat]) {
                        const uint64_t changed_pos = changed_write_offsets[flat]++;
                        write_changed_sample(changed_pos, static_cast<uint32_t>(global_row));
                    }
                } else {
                    has_prev_changed[flat] = 1;
                }
                prev_raw_changed[flat] = raw;
            }
        }
    }

    decoded_writer.finalize_directory();

    decoded_writer.publish_progress(sample_total, changed_sample_total, true);

    CBCM_INFO("Decode complete: %u dir entries, %llu samples, %llu changed samples from %llu entries (segmented)",
              dir_count,
              static_cast<unsigned long long>(sample_total),
              static_cast<unsigned long long>(changed_sample_total),
              static_cast<unsigned long long>(entry_count));

    decoded_writer.close();

    return 0;
}

extern "C" {

CD_EXPORT int32_t can_decoder_run(const char* parsed_mmap_token) {
    if (!parsed_mmap_token) {
        return -1;
    }

    file_service::ParsedMmapInterface parsed_mmap(parsed_mmap_token);
    return can_decoder_run(parsed_mmap);
}

} /* extern "C" */