
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "can_analyzer_log.h"
#include "can_decoder.h"
#include "can_log_decoder.h"
#include "sqlite/sql_decode_if.h"
#include "metadata_storage_if.h"

#ifndef LOGGING_TRACE_ENABLED
#if defined(__LW_TRACE)
#define LOGGING_TRACE_ENABLED CBCM_INFO("TRACE %s", __FUNCTION__);
#else
#define LOGGING_TRACE_ENABLED
#endif
#endif

//    Application must call before hand
//    CanDecoder::instance().load_db(messages,msg_count,signals,sig_count);
namespace {

constexpr int32_t kDecodeRcNullToken = -301;
constexpr int32_t kDecodeRcReadPageEmpty = -302;

CanDecoder& decoder_singleton() {
    static CanDecoder decoder;
    return decoder;
}

DecodeError make_decode_error(int32_t rc, const std::string& message) {
	DecodeError error{};
	error.rc = rc;
	std::snprintf(error.error_message, sizeof(error.error_message), "%s", message.c_str());
	return error;
}

DecodeError process_page(
    const std::vector<ParsedEntry>& page,
    uint64_t page_start_row,
    const CanDecoder& decoder,
    DecodedSignalDatabase& db,
    std::unordered_map<std::string, int64_t>& last_raw_by_signal,
    std::unordered_map<std::string, bool>& has_last_raw_by_signal)
{
    std::unordered_map<std::string, DecodedSignalChunk> signal_cache;

    for (size_t row = 0; row < page.size(); ++row) {
        const ParsedEntry& entry = page[row];
        const uint64_t global_row = page_start_row + row;

        const auto decoded = decoder.decode_entry(entry);
        for (const auto& sig : decoded) {
            const std::string signal_name = sig.signal_name;
            const std::string key = std::to_string(entry.can_id) + ":" + signal_name;
            auto& dst = signal_cache[key];

            dst.can_id = entry.can_id;
            dst.signal_name = signal_name;
            dst.row_index.push_back(static_cast<uint32_t>(global_row));
            dst.raw_value.push_back(sig.raw_value);
            dst.phys_value.push_back(sig.phys_value);

            const std::string state_key = key;
            const bool has_prev = has_last_raw_by_signal[state_key];
            if (has_prev && last_raw_by_signal[state_key] != sig.raw_value) {
                dst.changed_row_index.push_back(static_cast<uint32_t>(global_row));
            }
            has_last_raw_by_signal[state_key] = true;
            last_raw_by_signal[state_key] = sig.raw_value;
        }
    }

    std::vector<DecodedSignalChunk> chunks;
    chunks.reserve(signal_cache.size());
    for (auto& [key, signal] : signal_cache) {
        (void)key;
        chunks.push_back(std::move(signal));
    }

    const int32_t rc = db.write_signals(chunks);
    if (rc != 0) {
        return make_decode_error(rc, db.last_error_message());
    }
    return make_decode_error(0, "");
}

} // namespace

DecodeError can_decoder_run(const file_service::MetaDataStorageInterface& parsed_mmap, CanDatabaseModel model) {
    LOGGING_TRACE_ENABLED;

    CanDecoder& decoder = decoder_singleton();
    decoder.free_db();
    const int32_t load_rc = decoder.load_db(model);
    if (load_rc != 0) {
        CBCM_ERROR("can_decoder_run: failed to load CanDatabaseModel (rc=%d)", load_rc);
        return make_decode_error(load_rc, "can_decoder_run: failed to load CanDatabaseModel");
    }

    const uint64_t entry_count = parsed_mmap.fetch_count();
    if (entry_count == 0) {
        CBCM_ERROR("No parsed mmap data found (last_error=%d)", parsed_mmap.last_error_code());
        return make_decode_error(-2, "No parsed mmap data found");
    }

    constexpr uint64_t kReadChunkSize = 100'000;

    DecodedSignalDatabase signal_db(parsed_mmap.token_path());
    const int32_t open_rc = signal_db.open();
    if (open_rc != 0) {
        return make_decode_error(open_rc, signal_db.last_error_message());
    }
    const int32_t schema_rc = signal_db.initialize_schema();
    if (schema_rc != 0) {
        signal_db.close();
        return make_decode_error(schema_rc, signal_db.last_error_message());
    }
    const int32_t begin_rc = signal_db.begin_transaction();
    if (begin_rc != 0) {
        signal_db.close();
        return make_decode_error(begin_rc, signal_db.last_error_message());
    }

    std::unordered_map<std::string, int64_t> last_raw_by_signal;
    std::unordered_map<std::string, bool> has_last_raw_by_signal;

    for (uint64_t start = 0;
         start < entry_count;
         start += kReadChunkSize)
    {
        const uint64_t end_exclusive =
            std::min(
                start + kReadChunkSize,
                entry_count);

        std::vector<ParsedEntry> page =
            parsed_mmap.read_page(
                static_cast<int64_t>(start),
                static_cast<int64_t>(
                    end_exclusive - 1));

        if (page.empty())
        {
            const int32_t page_rc = parsed_mmap.last_error_code();
            signal_db.close();
            return make_decode_error(page_rc != 0 ? page_rc : kDecodeRcReadPageEmpty,
                                     page_rc != 0 ? "parsed mmap read_page failed" : "read_page returned empty page");
        }

        const DecodeError process_rc = process_page(
            page,
            start,
            decoder,
            signal_db,
            last_raw_by_signal,
            has_last_raw_by_signal);
        if (process_rc.rc != 0)
        {
            signal_db.close();
            return process_rc;
        }
    }

    const int32_t commit_rc = signal_db.commit_transaction();
    if (commit_rc != 0) {
        signal_db.close();
        return make_decode_error(commit_rc, signal_db.last_error_message());
    }
    signal_db.close();

    return make_decode_error(0, "");
}

DecodeError can_decoder_run(const char* parsed_mmap_token, CanDatabaseModel model) {
    if (!parsed_mmap_token) {
        return make_decode_error(kDecodeRcNullToken, "parsed_mmap_token is null");
    }

    file_service::MetaDataStorageInterface parsed_mmap(parsed_mmap_token);
    return can_decoder_run(parsed_mmap, std::move(model));
}

extern "C" {

CD_EXPORT DecodeError can_decoder_run(const char* parsed_mmap_token) {
    (void)parsed_mmap_token;
    CBCM_ERROR("can_decoder_run(const char*): deprecated without model; use can_decoder_run(token, model)");
    return make_decode_error(-3, "can_decoder_run(const char*): deprecated without model; use can_decoder_run(token, model)");
}

} /* extern "C" */