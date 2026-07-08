#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "mmap/mmap_data.h"
#include "parsed_entry_layout.h"

using namespace file_service::mmap;

TEST(DataMmapInterfaceTest, OpenAppendUpdateClose) {
    const auto tmp = std::filesystem::temp_directory_path();
    const std::string base = (tmp / "fs_test_mmap.mmap").string();

    // clean up any previous artifacts
    std::error_code ec;
    std::filesystem::remove(base + ".000.mmap", ec);
    std::filesystem::remove(base + ".index.sqlite", ec);

    DataMmapInterface dm(base);
    dm.set_source_file_path("/tmp/source.log");
    dm.open_and_init();

    LogRecord rec{};
    rec.timestamp = 2.345;
    rec.can_id = 0x456;
    rec.direction = 0;
    std::snprintf(rec.channel, sizeof(rec.channel), "%s", "can0");
    rec.data_len = 3;
    rec.data[0] = 1;
    rec.data[1] = 2;
    rec.data[2] = 3;

    const uint32_t row = dm.append_entry(rec);
    ASSERT_EQ(row, 0u);
    ASSERT_GE(dm.total_written(), 1u);

    dm.close_and_finalize();

    // Reopen reader to verify total rows
    DataMmapInterface dm2(base);
    uint64_t total = 0;
    dm2.read_total_rows(total);
    ASSERT_GE(total, 1u);
}
