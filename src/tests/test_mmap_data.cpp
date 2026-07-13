#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "mmap/mmap_data.h"
#include "mmap/mmap_layout_view.h"
#include "storage_token.h"
#include "parsed_entry_layout.h"

using namespace mmap;

TEST(DataMmapInterfaceTest, OpenAppendUpdateClose) {
    // StorageToken owns path derivation; the test only supplies a token id.
    StorageToken token("fs_test_mmap");
    const std::string base = token.mmap_path().string();

    // clean up any previous artifacts (first data segment)
    std::error_code ec;
    std::filesystem::remove(base + ".000", ec);

    DataMmapInterface<Access::ReadWrite> dm(base);

    ParsedEntry rec{};
    rec.timestamp = 2.345;
    rec.can_id = 0x456;
    rec.direction = 0;
    std::snprintf(rec.channel, sizeof(rec.channel), "%s", "can0");
    rec.data_len = 3;
    rec.data[0] = 1;
    rec.data[1] = 2;
    rec.data[2] = 3;

    const uint32_t row = dm.append_entry(rec);
    /// NOTE: appended row number 0th
    ASSERT_EQ(row, 0u);

    // Reopen reader to verify persisted rows
    DataMmapInterface<Access::ReadOnly> dm2(base);
    const auto entries = dm2.read_page(0, 10);
    ASSERT_GE(entries.size(), 1u);
    ASSERT_EQ(entries[0].can_id, rec.can_id);
}

TEST(DataMmapInterfaceTest, MultipleAppendAndNextIndex) {
    StorageToken token("fs_test_mmap_multi");
    const std::string base = token.mmap_path().string();

    // clean up previous artifacts
    std::error_code ec;
    std::filesystem::remove(base + ".000", ec);
    std::filesystem::remove(base + ".001", ec);
    std::filesystem::remove(base + ".002", ec);
    std::filesystem::remove(base + ".header", ec);

    DataMmapInterface<Access::ReadWrite> dm(base);

    ParsedEntry a{};
    a.timestamp = 1.0; a.can_id = 1; std::snprintf(a.channel, sizeof(a.channel), "%s", "can0");
    ParsedEntry b{};
    b.timestamp = 2.0; b.can_id = 2; std::snprintf(b.channel, sizeof(b.channel), "%s", "can1");
    ParsedEntry c{};
    c.timestamp = 3.0; c.can_id = 3; std::snprintf(c.channel, sizeof(c.channel), "%s", "can2");

    const uint32_t r0 = dm.append_entry(a);
    const uint32_t r1 = dm.append_entry(b);
    const uint32_t r2 = dm.append_entry(c);

    ASSERT_EQ(r0, 0u);
    ASSERT_EQ(r1, 1u);
    ASSERT_EQ(r2, 2u);

    // next_write_idx should reflect next index (3)
    ASSERT_EQ(dm.next_write_idx(), 3);

    DataMmapInterface<Access::ReadOnly> reader(base);
    const auto page = reader.read_page(0, 10);
    ASSERT_GE(page.size(), 3u);
    EXPECT_EQ(page[0].can_id, a.can_id);
    EXPECT_EQ(page[1].can_id, b.can_id);
    EXPECT_EQ(page[2].can_id, c.can_id);
}

TEST(DataMmapInterfaceTest, UpdateAndReadRows) {
    StorageToken token("fs_test_mmap_update");
    const std::string base = token.mmap_path().string();

    // clean up
    std::error_code ec;
    std::filesystem::remove(base + ".000", ec);
    std::filesystem::remove(base + ".header", ec);

    DataMmapInterface<Access::ReadWrite> dm(base);

    ParsedEntry rec{};
    rec.timestamp = 9.9;
    rec.can_id = 0x100;
    std::snprintf(rec.channel, sizeof(rec.channel), "%s", "canX");
    const uint32_t row = dm.append_entry(rec);
    ASSERT_EQ(row, 0u);

    // update
    ParsedEntry modified = rec;
    modified.can_id = 0x200;
    dm.update_entry(0, modified);

    // read single entry via out-param
    ParsedEntry out{};
    DataMmapInterface<Access::ReadOnly> reader(base);
    reader.read_entry_at(0, out);
    EXPECT_EQ(out.can_id, 0x200u);

    // read_rows
    std::vector<uint32_t> ids = {0u};
    const auto rows = reader.read_rows(ids);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].can_id, 0x200u);

    // out-of-range read should throw
    EXPECT_THROW(reader.read_entry_at(5, out), MMapError);
}

