#include <gtest/gtest.h>
#include <sstream>
#include <vector>

#include "asc_reader.h"
#include "parsed_entry_layout.h"

struct AscCase {
    std::string asc;
    double timestamp;
    uint32_t can_id;
    uint8_t direction;
    uint8_t data_len;
    std::vector<uint8_t> data;
    bool is_remote{false};
    bool is_fd{false};
    bool is_error{false};
};

class ASCReaderParamTest : public ::testing::TestWithParam<AscCase> {};

TEST_P(ASCReaderParamTest, Parses) {
    AscCase c = GetParam();
    std::istringstream iss(c.asc);
    ASCReader reader(iss, "hex", true);
    LogRecord out{};
    ASSERT_TRUE(reader.next(out));

    EXPECT_DOUBLE_EQ(out.timestamp, c.timestamp);
    EXPECT_EQ(out.can_id, c.can_id);
    EXPECT_EQ(out.direction, c.direction);
    EXPECT_EQ(out.data_len, c.data_len);
    EXPECT_EQ(out.is_remote_frame, c.is_remote);
    EXPECT_EQ(out.is_fd, c.is_fd);
    EXPECT_EQ(out.is_error_frame, c.is_error);

    ASSERT_GE(out.data_len, 0);
    for (size_t i = 0; i < c.data.size(); ++i) {
        EXPECT_EQ(out.data[i], c.data[i]);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Cases,
    ASCReaderParamTest,
    ::testing::Values(
        AscCase{ // 1 Classic CAN Tx
            "0.000000 1 1A5 Tx d 8 01 02 03 04 05 06 07 08\n",
            0.0, 0x1A5u, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{1,2,3,4,5,6,7,8}, false, false, false
        },
        AscCase{ // 2 Classic CAN Rx
            "0.001000 1 1A5 Rx d 8 08 07 06 05 04 03 02 01\n",
            0.001, 0x1A5u, static_cast<uint8_t>(1), static_cast<uint8_t>(8),
            std::vector<uint8_t>{8,7,6,5,4,3,2,1}, false, false, false
        },
        AscCase{ // 3 Classic CAN DLC=0
            "0.002000 1 123 Tx d 0\n",
            0.002, 0x123u, static_cast<uint8_t>(0), static_cast<uint8_t>(0),
            std::vector<uint8_t>{}, false, false, false
        },
        AscCase{ // 4 Classic CAN Remote
            "0.003000 1 123 Tx r 8\n",
            0.003, 0x123u, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{}, true, false, false
        },
        AscCase{ // 5 Classic CAN Remote Rx
            "0.004000 2 456 Rx r 0\n",
            0.004, 0x456u, static_cast<uint8_t>(1), static_cast<uint8_t>(0),
            std::vector<uint8_t>{}, true, false, false
        },
        AscCase{ // 6 Extended ID Tx
            "0.005000 1 18FF50E5x Tx d 8 11 22 33 44 55 66 77 88\n",
            0.005, 0x18FF50E5u, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88}, false, false, false
        },
        AscCase{ // 7 Extended ID Rx
            "0.006000 2 1FFFFFFFx Rx d 8 AA BB CC DD EE FF 00 11\n",
            0.006, 0x1FFFFFFFu, static_cast<uint8_t>(1), static_cast<uint8_t>(8),
            std::vector<uint8_t>{0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11}, false, false, false
        },
        AscCase{ // 8 Standard ID minimum
            "0.007000 1 0 Tx d 1 FF\n",
            0.007, 0x0u, static_cast<uint8_t>(0), static_cast<uint8_t>(1),
            std::vector<uint8_t>{0xFF}, false, false, false
        },
        AscCase{ // 9 Standard ID maximum
            "0.008000 1 7FF Rx d 8 01 02 03 04 05 06 07 08\n",
            0.008, 0x7FFu, static_cast<uint8_t>(1), static_cast<uint8_t>(8),
            std::vector<uint8_t>{1,2,3,4,5,6,7,8}, false, false, false
        },
        AscCase{ // 10 Extended ID maximum
            "0.009000 1 1FFFFFFFx Tx d 8 FF EE DD CC BB AA 99 88\n",
            0.009, 0x1FFFFFFFu, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88}, false, false, false
        },
        AscCase{ // 11 CAN FD Tx (no frame name)
            "0.010000 CANFD 1 Tx 123 1 0 8 8 01 02 03 04 05 06 07 08\n",
            0.010, 0x123u, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{1,2,3,4,5,6,7,8}, false, true, false
        },
        AscCase{ // 12 CAN FD Rx (no frame name)
            "0.011000 CANFD 2 Rx 456 0 0 12 12 01 02 03 04 05 06 07 08 09 0A 0B 0C\n",
            0.011, 0x456u, static_cast<uint8_t>(1), static_cast<uint8_t>(12),
            std::vector<uint8_t>{1,2,3,4,5,6,7,8,9,10,11,12}, false, true, false
        },
        AscCase{ // 13 CAN FD with frame name
            "0.012000 CANFD 1 Tx 123 EngineMsg 1 0 8 8 11 22 33 44 55 66 77 88\n",
            0.012, 0x123u, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88}, false, true, false
        },
        AscCase{ // 14 CAN FD with extended ID
            "0.013000 CANFD 1 Tx 18FF50E5x EngineMsg 1 1 12 12 01 02 03 04 05 06 07 08 09 0A 0B 0C\n",
            0.013, 0x18FF50E5u, static_cast<uint8_t>(0), static_cast<uint8_t>(12),
            std::vector<uint8_t>{1,2,3,4,5,6,7,8,9,10,11,12}, false, true, false
        },
        AscCase{ // 15 CAN FD Remote
            /// NOTE: Many loggers therefore omit the data bytes entirely.
            "0.014000 CANFD 1 Rx 123 0 0 0 0\n",
            0.014, 0x123u, static_cast<uint8_t>(1), static_cast<uint8_t>(0),
            std::vector<uint8_t>{}, true, true, false
        },
        AscCase{ // 16 CAN FD 64-byte payload
            "0.015000 CANFD 1 Tx 123 1 0 15 64 "
            "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
            "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F "
            "20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F "
            "30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F\n",
            0.015, 0x123u, static_cast<uint8_t>(0), static_cast<uint8_t>(64),
            [](){ std::vector<uint8_t> v; for (int i=0;i<64;++i) v.push_back(static_cast<uint8_t>(i)); return v;}(), false, true, false
        },

        /// NOTE: 
        /// An error frame is not a normal CAN frame.
        /// A normal CAN/CAN FD frame contains things like
        /// CAN ID
        /// DLC
        /// payload
        /// flags
        /// An error frame is simply reporting:
        /// "The controller detected an error on the bus."
        /// There is no CAN ID.
        /// There is no payload.
        /// There is no DLC.
        ///
        AscCase{ // 17 Classic ErrorFrame
            "0.016000 1 errorframe\n",
            0.016, 0u, static_cast<uint8_t>(0), static_cast<uint8_t>(0),
            std::vector<uint8_t>{}, false, false, true
        },
        AscCase{ // 18 CAN FD ErrorFrame
            "0.017000 CANFD 1 errorframe\n",
            0.017, 0u, static_cast<uint8_t>(0), static_cast<uint8_t>(0),
            std::vector<uint8_t>{}, false, true, true
        },
        AscCase{ // 19 Classic CAN with frame name
            "0.018000 1 321 Tx MyMessage 8 10 20 30 40 50 60 70 80\n",
            0.018, 0x321u, static_cast<uint8_t>(0), static_cast<uint8_t>(8),
            std::vector<uint8_t>{0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80}, false, false, false
        },
        AscCase{ // 20 Classic CAN Rx with frame name
            "0.019000 2 654 Rx VehicleSpeed 2 AA BB\n",
            0.019, 0x654u, static_cast<uint8_t>(1), static_cast<uint8_t>(2),
            std::vector<uint8_t>{0xAA,0xBB}, false, false, false
        }
    )
);

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
