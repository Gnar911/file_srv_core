#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <cstdio>

#include "view_browser.h"
#include "mmap/mmap_data.h"
#include "storage_token.h"
#include "parsed_entry_layout.h"

using namespace mmap;

TEST(ViewBrowserTest, UnfilteredIdentityView) {
    StorageToken token("fs_test_view_browser_unfiltered");
    const std::string base = token.mmap_path().string();

    // cleanup any previous artifacts
    std::error_code ec;
    std::filesystem::remove(base + ".000", ec);
    std::filesystem::remove(base + ".header", ec);

    DataMmapInterface<Access::ReadWrite> writer(base);

    ParsedEntry a{};
    a.timestamp = 1.0; a.can_id = 0x101; std::snprintf(a.channel, sizeof(a.channel), "%s", "can0");
    ParsedEntry b{};
    b.timestamp = 2.0; b.can_id = 0x202; std::snprintf(b.channel, sizeof(b.channel), "%s", "can1");

    const uint32_t r0 = writer.append_entry(a);
    const uint32_t r1 = writer.append_entry(b);

    ASSERT_EQ(r0, 0u);
    ASSERT_EQ(r1, 1u);

    // Reopen reader and build unfiltered ViewBrowser
    DataMmapInterface<Access::ReadOnly> reader(base);
    ViewBrowser vb(reader);
    vb.set_full_view(writer.next_write_idx());

    EXPECT_EQ(vb.size(), static_cast<size_t>(2));
    EXPECT_EQ(vb.at(0).can_id, a.can_id);
    EXPECT_EQ(vb.at(1).can_id, b.can_id);
}

TEST(ViewBrowserTest, FilteredView) {
    StorageToken token("fs_test_view_browser_filtered");
    const std::string base = token.mmap_path().string();

    std::error_code ec;
    std::filesystem::remove(base + ".000", ec);
    std::filesystem::remove(base + ".header", ec);

    DataMmapInterface<Access::ReadWrite> writer(base);

    ParsedEntry a{}; a.timestamp = 10.0; a.can_id = 0x11; std::snprintf(a.channel, sizeof(a.channel), "%s", "canA");
    ParsedEntry b{}; b.timestamp = 20.0; b.can_id = 0x22; std::snprintf(b.channel, sizeof(b.channel), "%s", "canB");
    ParsedEntry c{}; c.timestamp = 30.0; c.can_id = 0x33; std::snprintf(c.channel, sizeof(c.channel), "%s", "canC");

    writer.append_entry(a);
    writer.append_entry(b);
    writer.append_entry(c);

    DataMmapInterface<Access::ReadOnly> reader(base);

    // Create a filtered view containing only the middle entry (physical index 1)
    std::vector<uint32_t> rows = { 1u };
    ViewBrowser vb2(reader);
    vb2.set_rows(std::move(rows));

    EXPECT_EQ(vb2.size(), static_cast<size_t>(1));
    EXPECT_EQ(vb2.at(0).can_id, b.can_id);
}
