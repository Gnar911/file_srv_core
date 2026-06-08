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
std::string g_token_path;
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

TEST(ParsedMmapInterfaceApi, SegmentDiscovery) {
    if (g_token_path.empty()) {
        GTEST_SKIP() << "--token_path not provided";
    }

    const std::filesystem::path token_path = g_token_path;

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
