#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>

#include "parsed_entry_layout.h"
#include "parsed_mmap_if.h"

namespace {

std::string g_record_id;

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

ParsedEntry make_entry(uint32_t line, uint32_t can_id, double ts, const char* channel) {
    ParsedEntry e{};
    e.line_number = line;
    e.timestamp = ts;
    e.last_timestamp = ts;
    e.can_id = can_id;
    e.direction = 0;
    e.data_len = 2;
    e.changed = 0;
    e.data[0] = 0x12;
    e.data[1] = 0x34;
    std::snprintf(e.channel, sizeof(e.channel), "%s", channel);
    return e;
}

}  // namespace

namespace file_service {

class ParsedMmapInterfaceTestAccessor {
public:
    static std::vector<std::string> data_segment_paths(const ParsedMmapInterface& iface) {
        return iface.data_segment_paths();
    }

    static std::vector<std::string> canid_segment_paths(const ParsedMmapInterface& iface) {
        return iface.canid_segment_paths();
    }

    static std::vector<std::string> channel_segment_paths(const ParsedMmapInterface& iface) {
        return iface.channel_segment_paths();
    }

    static std::vector<std::string> direction_segment_paths(const ParsedMmapInterface& iface) {
        return iface.direction_segment_paths();
    }

    static ParsedMmapSegmentPaths all_segment_paths(const ParsedMmapInterface& iface) {
        return iface.all_segment_paths();
    }

    static uint64_t total_entries(const ParsedMmapInterface& iface) {
        return iface.data_.total_written();
    }

    static int32_t first_data_segment_capacity(const ParsedMmapInterface& iface, uint32_t& out_capacity) {
        const auto paths = iface.data_segment_paths();
        if (paths.empty()) {
            out_capacity = 0;
            return -1;
        }
        return iface.data_.read_segment_capacity(paths.front(), out_capacity);
    }

    static int32_t first_data_segment_write_count(const ParsedMmapInterface& iface, uint64_t& out_count) {
        const auto paths = iface.data_segment_paths();
        if (paths.empty()) {
            out_count = 0;
            return -1;
        }
        return iface.data_.read_segment_write_count(paths.front(), out_count);
    }
};

}  // namespace file_service
TEST(ParsedMmapInterfaceApi, WriterLifecycleAndApiSmokeCoverage) {
    const std::filesystem::path dir = make_test_temp_dir("parsed_mmap");
    const std::filesystem::path token_path = dir / "token_api";

    file_service::ParsedMmapInterface iface(token_path.string());

    std::vector<ParsedEntry> entries;
    entries.push_back(make_entry(1, 0x100, 1.0, "ch0"));
    entries.push_back(make_entry(2, 0x100, 2.0, "ch0"));
    entries.push_back(make_entry(3, 0x200, 3.0, "ch1"));

    EXPECT_EQ(iface.write_entries(entries), -18);
    ASSERT_EQ(iface.open_mmap(), 0);
    EXPECT_EQ(iface.write_entries(entries), 0);

    const auto data_paths = file_service::ParsedMmapInterfaceTestAccessor::data_segment_paths(iface);
    const auto canid_paths = file_service::ParsedMmapInterfaceTestAccessor::canid_segment_paths(iface);
    const auto channel_paths = file_service::ParsedMmapInterfaceTestAccessor::channel_segment_paths(iface);
    const auto direction_paths = file_service::ParsedMmapInterfaceTestAccessor::direction_segment_paths(iface);
    const auto all_paths = file_service::ParsedMmapInterfaceTestAccessor::all_segment_paths(iface);

    EXPECT_FALSE(data_paths.empty());
    EXPECT_FALSE(canid_paths.empty());
    EXPECT_FALSE(channel_paths.empty());
    EXPECT_FALSE(direction_paths.empty());
    EXPECT_FALSE(all_paths.data.empty());
    EXPECT_FALSE(all_paths.canid.empty());
    EXPECT_FALSE(all_paths.channel.empty());
    EXPECT_FALSE(all_paths.direction.empty());

    iface.close_mmap();

    EXPECT_EQ(iface.read_page(0, 10).size(), 3U);
    EXPECT_EQ(iface.read_page_from_can_id(0x100, 0, 10).size(), 2U);
    EXPECT_EQ(iface.read_page_from_can_ids({0x100, 0x200}, 0, 10).size(), 3U);
    EXPECT_EQ(iface.read_page_from_can_id_changed(0x100, 0, 10).size(), 0U);
    EXPECT_EQ(iface.read_page_from_can_ids_changed({0x100, 0x200}, 0, 10).size(), 0U);
    EXPECT_EQ(iface.read_page_from_channel("ch0", 0, 10).size(), 2U);
    EXPECT_EQ(iface.read_page_from_channels({"ch0", "ch1"}, 0, 10).size(), 3U);
    EXPECT_EQ(iface.read_page_from_direction("rx", 0, 10).size(), 3U);
    EXPECT_EQ(iface.read_page_from_directions({"rx", "tx"}, 0, 10).size(), 3U);

    cleanup_prefix_files(dir, token_path.filename().string());
}

TEST(ParsedMmapInterfaceApi, SegmentDiscovery) {
    if (g_record_id.empty()) {
        GTEST_SKIP() << "--record_id not provided";
    }

    std::filesystem::path token_path = g_record_id;
    if (!token_path.is_absolute()) {
        token_path = std::filesystem::temp_directory_path() / token_path;
    }

    file_service::ParsedMmapInterface iface(token_path.string());

    const auto all_paths = file_service::ParsedMmapInterfaceTestAccessor::all_segment_paths(iface);
    EXPECT_FALSE(all_paths.data.empty());
    EXPECT_FALSE(all_paths.canid.empty());
    EXPECT_FALSE(all_paths.channel.empty());
    EXPECT_FALSE(all_paths.direction.empty());

    std::cout << "[SegmentDiscovery] token=" << token_path.string() << "\n";
    for (const auto& p : all_paths.data) {
        std::cout << "[SegmentDiscovery] data_segment=" << p << "\n";
    }
    for (const auto& p : all_paths.canid) {
        std::cout << "[SegmentDiscovery] canid_segment=" << p << "\n";
    }
    for (const auto& p : all_paths.channel) {
        std::cout << "[SegmentDiscovery] channel_segment=" << p << "\n";
    }
    for (const auto& p : all_paths.direction) {
        std::cout << "[SegmentDiscovery] direction_segment=" << p << "\n";
    }

    const uint64_t total_entries = iface.get_total_entries_num();
    EXPECT_GT(total_entries, 0U);
    std::cout << "[SegmentDiscovery] total_entries=" << static_cast<unsigned long long>(total_entries) << "\n";

    uint32_t capacity = 0;
    uint64_t write_count = 0;
    EXPECT_EQ(file_service::ParsedMmapInterfaceTestAccessor::first_data_segment_capacity(iface, capacity), 0);
    EXPECT_EQ(file_service::ParsedMmapInterfaceTestAccessor::first_data_segment_write_count(iface, write_count), 0);
    EXPECT_GT(capacity, 0U);
    EXPECT_GT(write_count, 0U);
    std::cout << "[SegmentDiscovery] first_segment_capacity=" << capacity << "\n";
    std::cout << "[SegmentDiscovery] first_segment_write_count=" << static_cast<unsigned long long>(write_count) << "\n";
}

int main(int argc, char** argv) {
    std::vector<char*> gtest_argv;
    gtest_argv.reserve(static_cast<size_t>(argc));
    gtest_argv.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        static constexpr const char* kRecordIdPrefix = "--record_id=";
        if (arg.rfind(kRecordIdPrefix, 0) == 0) {
            g_record_id = arg.substr(std::char_traits<char>::length(kRecordIdPrefix));
            continue;
        }
        gtest_argv.push_back(argv[i]);
    }

    int gtest_argc = static_cast<int>(gtest_argv.size());
    ::testing::InitGoogleTest(&gtest_argc, gtest_argv.data());
    return RUN_ALL_TESTS();
}
