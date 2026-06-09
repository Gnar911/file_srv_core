#include <gtest/gtest.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "can_decoder.h"
#include "can_log_decoder.h"
#include "sqlite/sql_decode_if.h"

namespace {
std::string g_token_path;
}

TEST(DecodeFunctionTest, RunDecodeAndConfirmDatabase) {
    if (g_token_path.empty()) {
        GTEST_SKIP() << "--token_path not provided";
    }

    const std::string token_path = g_token_path;
    std::cout << "[DecodeFunctionTest] token_path=" << token_path << "\n";

    CanDecoder decoder;
    decoder.free_db();

    MessageDef msg{};
    msg.can_id = 0x123;
    msg.signal_count = 1;
    msg.msg_length = 8;
    msg.signal_offset = 0;

    SignalDef sig{};
    sig.start_bit = 0;
    sig.bit_length = 8;
    sig.byte_order = 0;
    sig.is_signed = 0;
    sig.has_choices = 0;
    sig.scale = 1.0;
    sig.offset = 0.0;

    CanDatabaseModel model{};
    model.messages.push_back(msg);
    model.signals.push_back(sig);
    model.canid_to_msg[msg.can_id] = 0;
    ASSERT_EQ(decoder.load_db(model), 0);
    
    const DecodeError decode_error = can_decoder_run(token_path.c_str(), decoder);
    std::cout << "[DecodeFunctionTest] can_decoder_run rc=" << decode_error.rc
              << " error=" << decode_error.error_message << "\n";
    ASSERT_EQ(decode_error.rc, 0) << "can_decoder_run failed with rc=" << decode_error.rc
                                  << " error=" << decode_error.error_message;

    const std::filesystem::path db_path = token_path + ".decoded.sqlite";
    std::cout << "[DecodeFunctionTest] expected db_path=" << db_path << "\n";
    ASSERT_TRUE(std::filesystem::exists(db_path))
        << "decoded sqlite db not found: " << db_path;
    EXPECT_GT(std::filesystem::file_size(db_path), 0U)
        << "decoded sqlite db is empty: " << db_path;

    DecodedSignalDatabase db(token_path);
    ASSERT_EQ(db.open(), 0) << "DecodedSignalDatabase::open() failed";
    std::cout << "[DecodeFunctionTest] DecodedSignalDatabase opened: " << db.db_path() << "\n";
    db.close();
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
