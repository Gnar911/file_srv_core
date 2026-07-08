#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "sqlite/log_index_db.h"
#include "parsed_entry_layout.h"

using namespace file_service;

TEST(LogIndexDatabaseTest, OpenInitializeAppend) {
    const auto tmp = std::filesystem::temp_directory_path();
    const std::string token = (tmp / "fs_test_logindex").string();

    // remove previous artifacts if any
    std::error_code ec;
    std::filesystem::remove(token + ".index.sqlite", ec);

    file_service::LogIndexDatabase db(token);
    // open + initialize schema
    db.open_append_session();
    ASSERT_EQ(db.initialize_schema(), 0);

    db.begin_transaction();

    LogRecord rec{};
    rec.timestamp = 1.234;
    rec.can_id = 0x123;
    rec.direction = 1;
    std::snprintf(rec.channel, sizeof(rec.channel), "%s", "can0");
    rec.data_len = 2;
    rec.data[0] = 0xAA;
    rec.data[1] = 0xBB;

    db.append_entry(0u, rec);
    db.commit_transaction();
    db.close_append_session();

    // verify row exists by opening DB directly and counting rows
    sqlite3* raw_db = nullptr;
    sqlitew::open((token + ".index.sqlite").c_str(), &raw_db);
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
