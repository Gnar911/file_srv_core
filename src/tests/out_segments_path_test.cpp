#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>

#include "can_decoder.h"
#include "can_log_decoder.h"
#include "parsed_entry_layout.h"
#include "metadata_storage_if.h"
#include "sqlite/sql_decode_if.h"

namespace {
std::string g_token_path;

std::filesystem::path make_test_temp_dir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("file_service_core_" + name);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void cleanup_prefix_files(const std::filesystem::path& dir, const std::string& prefix) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

LogRecord make_entry(uint32_t line, uint32_t can_id, uint8_t b0) {
    LogRecord e{};
    e.timestamp = static_cast<double>(line);
    e.can_id = can_id;
    e.direction = 0;
    e.data_len = 1;
    e.data[0] = b0;
    std::snprintf(e.channel, sizeof(e.channel), "%s", "ch0");
    return e;
}
}  // namespace


class MetaDataStorageInterfaceTestAccessor {
public:
    static std::vector<std::string> data_segment_paths(const MetaDataStorageInterface& iface) {
        return iface.data_segment_paths();
    }

    static uint64_t total_entries(const MetaDataStorageInterface& iface) {
        return iface.data_.total_written();
    }

    static int32_t first_data_segment_capacity(const MetaDataStorageInterface& iface, uint32_t& out_capacity) {
        const auto paths = iface.data_segment_paths();
        if (paths.empty()) {
            out_capacity = 0;
            return -1;
        }
        return iface.data_.read_segment_capacity(paths.front(), out_capacity);
    }

    static int32_t first_data_segment_write_count(const MetaDataStorageInterface& iface, uint64_t& out_count) {
        const auto paths = iface.data_segment_paths();
        if (paths.empty()) {
            out_count = 0;
            return -1;
        }
        return iface.data_.read_segment_write_count(paths.front(), out_count);
    }
};


TEST(ParsedMmapInterfaceApi, SegmentDiscovery) {
    if (g_token_path.empty()) {
        GTEST_SKIP() << "--token_path not provided";
    }

    const std::filesystem::path token_path = g_token_path;

    MetaDataStorageInterface iface(token_path.string());

    const auto data_paths = MetaDataStorageInterfaceTestAccessor::data_segment_paths(iface);
    EXPECT_FALSE(data_paths.empty());

    std::cout << "[SegmentDiscovery] token=" << token_path.string() << "\n";
    for (const auto& p : data_paths) {
        std::cout << "[SegmentDiscovery] data_segment=" << p << "\n";
    }

    const uint64_t total_entries = iface.fetch_count();
    EXPECT_GT(total_entries, 0U);
    std::cout << "[SegmentDiscovery] total_entries=" << static_cast<unsigned long long>(total_entries) << "\n";

    uint32_t capacity = 0;
    uint64_t write_count = 0;
    EXPECT_EQ(MetaDataStorageInterfaceTestAccessor::first_data_segment_capacity(iface, capacity), 0);
    EXPECT_EQ(MetaDataStorageInterfaceTestAccessor::first_data_segment_write_count(iface, write_count), 0);
    EXPECT_GT(capacity, 0U);
    EXPECT_GT(write_count, 0U);
    std::cout << "[SegmentDiscovery] first_segment_capacity=" << capacity << "\n";
    std::cout << "[SegmentDiscovery] first_segment_write_count=" << static_cast<unsigned long long>(write_count) << "\n";
}

TEST(CanDecoderApiMock, DecodeEntryUsesTextSignalName) {
    CanDecoder decoder;
    MessageDef msg{};
    msg.can_id = 0x123;
    msg.signal_count = 1;
    msg.msg_length = 8;
    msg.signal_offset = 0;

    SignalDef sig{};
    sig.start_bit = 0;
    sig.bit_length = 8;
    sig.byte_order = 0;
    sig.is_signed = 0;
    sig.has_choices = 0;
    sig.scale = 1.0;
    sig.offset = 0.0;

    CanDatabaseModel model{};
    model.messages.push_back(msg);
    model.signals.push_back(sig);
    model.canid_to_msg[msg.can_id] = 0;
    ASSERT_EQ(decoder.load_db(model), 0);

    ParsedEntry entry{};
    entry.can_id = 0x123;
    entry.data_len = 1;
    entry.data[0] = 0x2A;
    std::snprintf(entry.channel, sizeof(entry.channel), "%s", "ch0");

    const auto decoded = decoder.decode_entry(entry);
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded[0].signal_name, "signal_0");
    EXPECT_EQ(decoded[0].raw_value, 42);
    EXPECT_DOUBLE_EQ(decoded[0].phys_value, 42.0);

    decoder.free_db();
}

TEST(CanDecoderApiMock, RunDecodeSmokeWritesSqliteData) {
    const std::filesystem::path dir = make_test_temp_dir("run_decode_smoke");
    const std::filesystem::path token_path = dir / "token_run_decode";

    MetaDataStorageInterface parsed(token_path.string());
    parsed.close_storage();

    std::vector<LogRecord> entries;
    entries.push_back(make_entry(1, 0x321, 10));
    entries.push_back(make_entry(2, 0x321, 10));
    entries.push_back(make_entry(3, 0x321, 20));

    parsed.write_entries(entries);
    parsed.close_storage();

    CanDecoder decoder;
    decoder.free_db();

    MessageDef msg{};
    msg.can_id = 0x321;
    msg.signal_count = 1;
    msg.msg_length = 8;
    msg.signal_offset = 0;

    SignalDef sig{};
    sig.start_bit = 0;
    sig.bit_length = 8;
    sig.byte_order = 0;
    sig.is_signed = 0;
    sig.scale = 1.0;
    sig.offset = 0.0;

    CanDatabaseModel model{};
    model.messages.push_back(msg);
    model.signals.push_back(sig);
    model.canid_to_msg[msg.can_id] = 0;
    ASSERT_EQ(can_decoder_run(token_path.string().c_str(), model).rc, 0);

    const std::filesystem::path db_path = token_path.string() + ".decoded.sqlite";
    EXPECT_TRUE(std::filesystem::exists(db_path));
    EXPECT_GT(std::filesystem::file_size(db_path), 0U);

    DecodedSignalDatabase db(token_path.string());
    ASSERT_EQ(db.open(), 0);

    const auto names = db.get_signal_names(0x321);
    ASSERT_EQ(names.size(), 1U);
    EXPECT_EQ(names[0], "signal_0");

    const auto chunk = db.get_signal_samples(0x321, "signal_0");
    ASSERT_EQ(chunk.row_index.size(), 3U);
    ASSERT_EQ(chunk.raw_value.size(), 3U);
    ASSERT_EQ(chunk.phys_value.size(), 3U);
    ASSERT_EQ(chunk.changed_row_index.size(), 1U);

    EXPECT_EQ(chunk.row_index[0], 0U);
    EXPECT_EQ(chunk.row_index[1], 1U);
    EXPECT_EQ(chunk.row_index[2], 2U);
    EXPECT_EQ(chunk.raw_value[0], 10);
    EXPECT_EQ(chunk.raw_value[1], 10);
    EXPECT_EQ(chunk.raw_value[2], 20);
    EXPECT_DOUBLE_EQ(chunk.phys_value[0], 10.0);
    EXPECT_DOUBLE_EQ(chunk.phys_value[1], 10.0);
    EXPECT_DOUBLE_EQ(chunk.phys_value[2], 20.0);
    EXPECT_EQ(chunk.changed_row_index[0], 2U);

    db.close();
    cleanup_prefix_files(dir, token_path.filename().string());
}

TEST(DecodedSqliteApi, DatabaseFileDiscoveryByTokenPath) {
    if (g_token_path.empty()) {
        GTEST_SKIP() << "--token_path not provided";
    }

    const std::filesystem::path token_path = g_token_path;
    const std::filesystem::path db_path = token_path.string() + ".decoded.sqlite";
    EXPECT_TRUE(std::filesystem::exists(db_path));
    EXPECT_GT(std::filesystem::file_size(db_path), 0U);

    DecodedSignalDatabase db(token_path.string());
    ASSERT_EQ(db.open(), 0);
    db.close();
}

int main(int argc, char** argv) {
    std::vector<char*> gtest_argv;
    gtest_argv.reserve(static_cast<size_t>(argc));
    gtest_argv.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        static constexpr const char* kTokenPathPrefix = "--token_path=";
        if (arg.rfind(kTokenPathPrefix, 0) == 0) {
            g_token_path = arg.substr(std::char_traits<char>::length(kTokenPathPrefix));
            continue;
        }
        gtest_argv.push_back(argv[i]);
    }

    int gtest_argc = static_cast<int>(gtest_argv.size());
    ::testing::InitGoogleTest(&gtest_argc, gtest_argv.data());
    return RUN_ALL_TESTS();
}
