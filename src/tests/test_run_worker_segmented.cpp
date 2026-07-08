#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>

#include "can_parser.h"
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

    const std::string token_prefix = tmpdir + "/token";
    const std::string file_path = tmpdir + "/input.asc";

    // write ~100 lines by repeating the mock lines
    std::ofstream ofs(file_path);
    ASSERT_TRUE(ofs.good());
    for (int rep = 0; rep < 5; ++rep) {
        for (const auto& l : kMockAscLines) {
            ofs << l << "\n";
        }
    }
    ofs.close();

    // ensure parent directory of token_prefix exists (it does, tmpdir)

    int rc = run_worker_segmented(file_path.c_str(), token_prefix.c_str());
    EXPECT_EQ(rc, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
