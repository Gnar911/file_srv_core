#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include "sqlite/log_index_db.h"
#include "storage_token.h"
#include "parsed_entry_layout.h"


TEST(LogIndexDatabaseTest, OpenInitializeAppend) {
    // StorageToken owns path derivation; the test only supplies a token id.
    StorageToken token("fs_test_logindex");
    const std::string db_path = token.sqlite_path().string();

    // remove previous artifacts if any
    std::error_code ec;
    std::filesystem::remove(db_path, ec);

    LogIndexDatabase db(db_path);

    db.begin_transaction();

    ParsedEntry rec{};
    rec.timestamp = 1.234;
    rec.can_id = 0x123;
    rec.direction = 1;
    std::snprintf(rec.channel, sizeof(rec.channel), "%s", "can0");
    rec.data_len = 2;
    rec.data[0] = 0xAA;
    rec.data[1] = 0xBB;
    rec.changed = 1;

    db.append_index(0u, rec);
    db.commit_transaction();

    // verify row exists by opening DB directly and counting rows
    sqlite3* raw_db = nullptr;
    sqlitew::open(db_path.c_str(), &raw_db);
    sqlite3_stmt* stmt = nullptr;
    sqlitew::prepare_v2(raw_db, "SELECT COUNT(*) FROM log_index;", -1, &stmt, nullptr);
    const int rc = sqlitew::step(stmt);
    // SQLITE_ROW is 100 in wrapper implementation
    ASSERT_EQ(rc, 100);
    const long long cnt = sqlitew::column_int64(stmt, 0);
    ASSERT_GE(cnt, 1);
    sqlitew::finalize(stmt);
    sqlitew::close(raw_db);
}

TEST(LogIndexDatabaseTest, QueryRowIndicesAndStats) {
    StorageToken token("fs_test_logindex_query");
    const std::string db_path = token.sqlite_path().string();

    // remove previous artifacts if any
    std::error_code ec;
    std::filesystem::remove(db_path, ec);

    LogIndexDatabase db(db_path);
    db.begin_transaction();

    // Insert several entries with different attributes
    ParsedEntry e0{}; e0.timestamp = 10.0; e0.can_id = 1; e0.direction = 0; std::snprintf(e0.channel, sizeof(e0.channel), "%s", "can0"); e0.changed = 1;
    ParsedEntry e1{}; e1.timestamp = 20.0; e1.can_id = 2; e1.direction = 1; std::snprintf(e1.channel, sizeof(e1.channel), "%s", "can1"); e1.changed = 0;
    ParsedEntry e2{}; e2.timestamp = 30.0; e2.can_id = 1; e2.direction = 0; std::snprintf(e2.channel, sizeof(e2.channel), "%s", "can0"); e2.changed = 0;
    ParsedEntry e3{}; e3.timestamp = 40.0; e3.can_id = 3; e3.direction = 1; std::snprintf(e3.channel, sizeof(e3.channel), "%s", "can2"); e3.changed = 1;
    ParsedEntry e4{}; e4.timestamp = 50.0; e4.can_id = 1; e4.direction = 0; std::snprintf(e4.channel, sizeof(e4.channel), "%s", "can0"); e4.changed = 1;

    db.append_index(0u, e0);
    db.append_index(1u, e1);
    db.append_index(2u, e2);
    db.append_index(3u, e3);
    db.append_index(4u, e4);

    db.commit_transaction();

    // row_count and first/last timestamps
    EXPECT_EQ(db.row_count(), 5u);
    double first_ts = 0, last_ts = 0;
    EXPECT_TRUE(db.get_first_last_timestamp(first_ts, last_ts));
    EXPECT_EQ(first_ts, 10.0);
    EXPECT_EQ(last_ts, 50.0);

    // Query by can_id == 1
    LogQuery q1; q1.can_ids = {1};
    auto rows_can1 = db.query_row_indices(q1, 0, 10);
    ASSERT_EQ(rows_can1.size(), 3u);
    EXPECT_EQ(rows_can1[0], 0u);
    EXPECT_EQ(rows_can1[1], 2u);
    EXPECT_EQ(rows_can1[2], 4u);

    // changed_only filter
    LogQuery q_changed; q_changed.changed_only = true;
    auto rows_changed = db.query_row_indices(q_changed, 0, 10);
    // entries with changed==1 are indices 0,3,4
    ASSERT_EQ(rows_changed.size(), 3u);
    EXPECT_EQ(rows_changed[0], 0u);
    EXPECT_EQ(rows_changed[1], 3u);
    EXPECT_EQ(rows_changed[2], 4u);

    // time range 15..45 -> should return indices 1,2,3
    LogQuery q_time; q_time.has_time_range = true; q_time.first_ts = 15.0; q_time.last_ts = 45.0;
    auto rows_time = db.query_row_indices(q_time, 0, 10);
    ASSERT_EQ(rows_time.size(), 3u);
    EXPECT_EQ(rows_time[0], 1u);
    EXPECT_EQ(rows_time[1], 2u);
    EXPECT_EQ(rows_time[2], 3u);
}

// Verify that `row_count()` remains usable while another thread is appending
// rows. The writer commits after each insert so readers can observe growth.
TEST(LogIndexDatabaseTest, ConcurrentAppendRowCount) {
    StorageToken token("fs_test_logindex_concurrent");
    const std::string db_path = token.sqlite_path().string();

    // remove previous artifacts if any
    std::error_code ec;
    std::filesystem::remove(db_path, ec);

    LogIndexDatabase db(db_path);

    const int kNumRows = 100;
    std::atomic<bool> writer_done{false};
    std::exception_ptr writer_ex = nullptr;

    // Writer thread: append kNumRows entries, committing each one so readers
    // can observe incremental growth.
    auto writer = [&]() {
        try {
            for (int i = 0; i < kNumRows; ++i) {
                db.begin_transaction();
                ParsedEntry e{};
                e.timestamp = static_cast<double>(i + 1);
                e.can_id = static_cast<uint32_t>(i % 8);
                e.direction = static_cast<uint8_t>(i % 2);
                std::snprintf(e.channel, sizeof(e.channel), "%s", "canX");
                e.changed = (i % 3 == 0) ? 1 : 0;
                db.append_index(static_cast<uint32_t>(i), e);
                db.commit_transaction();
                // small pause to amplify concurrent observation window
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        } catch (...) {
            writer_ex = std::current_exception();
        }
        writer_done.store(true, std::memory_order_release);
    };

    // Reader thread: poll row_count and assert non-decreasing values until
    // writer finishes and the final expected count is reached.
    auto reader = [&]() {
        uint32_t last_seen = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!writer_done.load(std::memory_order_acquire) || last_seen < static_cast<uint32_t>(kNumRows)) {
            const uint32_t cnt = db.row_count();
            // row_count() should never shrink for this append-only test
            EXPECT_GE(cnt, last_seen);
            last_seen = cnt;
            if (last_seen >= static_cast<uint32_t>(kNumRows)) {
                break;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // final sanity check
        EXPECT_GE(last_seen, static_cast<uint32_t>(kNumRows));
    };

    std::thread tw(writer);
    std::thread tr(reader);

    tw.join();
    tr.join();

    if (writer_ex) {
        std::rethrow_exception(writer_ex);
    }

    EXPECT_EQ(db.row_count(), static_cast<uint32_t>(kNumRows));
}
