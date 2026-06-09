#pragma once

#include <cstdint>

#ifndef CD_EXPORT
#if defined(_WIN32)
#define CD_EXPORT __declspec(dllexport)
#else
#define CD_EXPORT
#endif
#endif

#ifdef __cplusplus
class CanDecoder;
namespace file_service {
class ParsedMmapInterface;
}

struct DecodeError {
	int32_t rc = 0;
	char error_message[512] = {0};
};

DecodeError can_decoder_run(const file_service::ParsedMmapInterface& parsed_mmap,
						const CanDecoder& decoder);
DecodeError can_decoder_run(const char* parsed_mmap_token,
						const CanDecoder& decoder);

extern "C" {
#endif

CD_EXPORT DecodeError can_decoder_run(const char* parsed_mmap_token);

#ifdef __cplusplus
}
#endif
