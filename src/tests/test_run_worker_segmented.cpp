#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>

#include "can_parser.h"
#include "metadata_storage_if.h"
#include <thread>
#include <atomic>
#include <chrono>
static const std::vector<std::string> kMockAscLines = {
    "0.000000 1 1A5 Tx d 8 01 02 03 04 05 06 07 08",
    "0.001000 1 1A5 Rx d 8 08 07 06 05 04 03 02 01",
    "0.002000 1 123 Tx d 0",
    "0.003000 1 123 Tx r 8",
    "0.004000 2 456 Rx r 0",
    "0.005000 1 18FF50E5x Tx d 8 11 22 33 44 55 66 77 88",
    "0.006000 2 1FFFFFFFx Rx d 8 AA BB CC DD EE FF 00 11",
    "0.007000 1 0 Tx d 1 FF",
    "0.008000 1 7FF Rx d 8 01 02 03 04 05 06 07 08",
    "0.009000 1 1FFFFFFFx Tx d 8 FF EE DD CC BB AA 99 88",
    "0.010000 CANFD 1 Tx 123 1 0 8 8 01 02 03 04 05 06 07 08",
    "0.011000 CANFD 2 Rx 456 0 0 12 12 01 02 03 04 05 06 07 08 09 0A 0B 0C",
    "0.012000 CANFD 1 Tx 123 EngineMsg 1 0 8 8 11 22 33 44 55 66 77 88",
    "0.013000 CANFD 1 Tx 18FF50E5x EngineMsg 1 1 12 12 01 02 03 04 05 06 07 08 09 0A 0B 0C",
    "0.014000 CANFD 1 Rx 123 0 0 0 0",
    "0.015000 CANFD 1 Tx 123 1 0 15 64 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F",
    "0.016000 1 errorframe",
    "0.017000 CANFD 1 errorframe",
    "0.018000 1 321 Tx MyMessage 8 10 20 30 40 50 60 70 80",
    "0.019000 2 654 Rx VehicleSpeed 2 AA BB"
};

TEST(RunWorkerSegmented, WritesArtifacts) {
    // create a unique temp directory
    std::string tmpl = std::string("/tmp/run_worker_segmented_test_XXXXXX");
    char* dir = mkdtemp(&tmpl[0]);
    ASSERT_NE(dir, nullptr);
    std::string tmpdir(dir);

    // StorageToken (inside run_worker_segmented) owns output path derivation;
    // the test only supplies a token id. Only the input file needs a real path.
    const std::string token_id = "run_worker_segmented_test_token";
    const std::string file_path = tmpdir + "/input.asc";

    // Debug: print resolved StorageToken paths to see where files will be created
            StorageToken token(token_id);
            std::cout << "StorageToken.root=" << token.root() << "\n";
            std::cout << "StorageToken.sqlite_path=" << token.sqlite_path() << "\n";
            std::cout << "StorageToken.mmap_path=" << token.mmap_path() << "\n";
            std::cout << "StorageToken.base_path=" << token.base_path() << "\n";

    // write ~100 lines by repeating the mock lines
    std::ofstream ofs(file_path);
    ASSERT_TRUE(ofs.good());
    for (int rep = 0; rep < 5; ++rep) {
        for (const auto& l : kMockAscLines) {
            ofs << l << "\n";
        }
    }
    ofs.close();

    // Compute the EXPECTED records from the exact same input using the same
    // reader path the worker uses (parse_lines -> ASCReader). This is the
    // ground truth we wrote into the file; the worker must store exactly these.
    std::string file_contents;
    {
        std::ifstream ifs(file_path);
        ASSERT_TRUE(ifs.good());
        std::stringstream ss;
        ss << ifs.rdbuf();
        file_contents = ss.str();
    }
    const std::vector<LogRecord> expected = parse_lines(file_contents);
    ASSERT_FALSE(expected.empty());

    // Expected first/last timestamps are the min/max over the written rows.
    double expected_first_ts = expected.front().timestamp;
    double expected_last_ts = expected.front().timestamp;
    for (const auto& e : expected) {
        expected_first_ts = std::min(expected_first_ts, e.timestamp);
        expected_last_ts = std::max(expected_last_ts, e.timestamp);
    }

    int rc = run_worker_segmented(file_path.c_str(), token_id.c_str());
    ASSERT_EQ(rc, 0);

    MetaDataStorageInterface mdi(token_id);

    // 1) Row count stored must equal the number of input mock records.
    ASSERT_EQ(mdi.fetch_count(), static_cast<uint32_t>(expected.size()));

    // 2) Metadata must reflect the written rows exactly.
    const auto md = mdi.get_metadata();
    EXPECT_EQ(md.total_rows, static_cast<uint32_t>(expected.size()));
    EXPECT_DOUBLE_EQ(md.first_timestamp, expected_first_ts);
    EXPECT_DOUBLE_EQ(md.last_timestamp, expected_last_ts);
    EXPECT_EQ(md.source_file_path, file_path);

    // 3) Every stored row must match the corresponding input record field-by-field.
    const std::vector<ParsedEntry> stored =
        mdi.read_page(0, static_cast<int32_t>(expected.size()));
    ASSERT_EQ(stored.size(), expected.size());

    for (size_t i = 0; i < expected.size(); ++i) {
        const LogRecord& want = expected[i];
        const ParsedEntry& got = stored[i];
        EXPECT_DOUBLE_EQ(got.timestamp, want.timestamp) << "row " << i;
        EXPECT_EQ(got.can_id, want.can_id) << "row " << i;
        EXPECT_EQ(got.direction, want.direction) << "row " << i;
        EXPECT_EQ(got.data_len, want.data_len) << "row " << i;
        EXPECT_EQ(got.is_extended_id, want.is_extended_id) << "row " << i;
        EXPECT_EQ(got.is_error_frame, want.is_error_frame) << "row " << i;
        EXPECT_EQ(got.is_remote_frame, want.is_remote_frame) << "row " << i;
        EXPECT_EQ(got.is_fd, want.is_fd) << "row " << i;
        const uint8_t n = std::min<uint8_t>(want.data_len, static_cast<uint8_t>(sizeof(want.data)));
        EXPECT_EQ(0, std::memcmp(got.data, want.data, n)) << "row " << i << " payload";
        EXPECT_STREQ(got.channel, want.channel) << "row " << i;
    }
}

TEST(RunWorkerSegmented, ConcurrentGetMetadata) {
    // create a unique temp directory
    std::string tmpl = std::string("/tmp/run_worker_segmented_test_XXXXXX");
    char* dir = mkdtemp(&tmpl[0]);
    ASSERT_NE(dir, nullptr);
    std::string tmpdir(dir);

    const std::string token_id = "run_worker_segmented_test_token";
    const std::string file_path = tmpdir + "/input.asc";

    // write input file (same repetition as other test)
    std::ofstream ofs(file_path);
    ASSERT_TRUE(ofs.good());
    for (int rep = 0; rep < 5; ++rep) {
        for (const auto& l : kMockAscLines) {
            ofs << l << "\n";
        }
    }
    ofs.close();

    // Compute expected records
    std::string file_contents;
    {
        std::ifstream ifs(file_path);
        ASSERT_TRUE(ifs.good());
        std::stringstream ss;
        ss << ifs.rdbuf();
        file_contents = ss.str();
    }
    const std::vector<LogRecord> expected = parse_lines(file_contents);
    ASSERT_FALSE(expected.empty());

    std::atomic<int> worker_rc{-1};
    std::atomic<bool> worker_done{false};

    // Worker thread: run the segmented worker
    std::thread worker([&]() {
        try {
            int rc = run_worker_segmented(file_path.c_str(), token_id.c_str());
            worker_rc.store(rc);
        } catch (const std::exception& e) {
            std::cerr << "worker thread exception: " << e.what() << std::endl;
            worker_rc.store(-2);
        } catch (...) {
            std::cerr << "worker thread unknown exception" << std::endl;
            worker_rc.store(-3);
        }
        worker_done.store(true);
    });

    // Metadata thread: poll get_metadata() while worker runs
    std::atomic<bool> saw_progress{false};
    std::thread reader([&]() {
        MetaDataStorageInterface mdi(token_id);
        // Poll until worker completes or timeout
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!worker_done.load() && std::chrono::steady_clock::now() < deadline) {
            auto md = mdi.get_metadata();
            if (md.total_rows > 0) saw_progress.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    worker.join();
    reader.join();

    // Ensure the metadata reader observed progress while the worker ran.
    ASSERT_TRUE(saw_progress.load());

    ASSERT_EQ(worker_rc.load(), 0);

    MetaDataStorageInterface mdi(token_id);
    ASSERT_EQ(mdi.fetch_count(), static_cast<uint32_t>(expected.size()));

    const auto md = mdi.get_metadata();
    EXPECT_EQ(md.total_rows, static_cast<uint32_t>(expected.size()));

    const std::vector<ParsedEntry> stored = mdi.read_page(0, static_cast<int32_t>(expected.size()));
    ASSERT_EQ(stored.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        const LogRecord& want = expected[i];
        const ParsedEntry& got = stored[i];
        EXPECT_DOUBLE_EQ(got.timestamp, want.timestamp) << "row " << i;
        EXPECT_EQ(got.can_id, want.can_id) << "row " << i;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
