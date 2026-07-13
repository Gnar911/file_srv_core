#pragma once

#include <cstdint>
#include "can_decoder.h"

#ifndef CD_EXPORT
#if defined(_WIN32)
#define CD_EXPORT __declspec(dllexport)
#else
#define CD_EXPORT
#endif
#endif

#ifdef __cplusplus
class MetaDataStorageInterface;

struct DecodeError {
	int32_t rc = 0;
	char error_message[512] = {0};
};

DecodeError can_decoder_run(const MetaDataStorageInterface& parsed_mmap,
						CanDatabaseModel model);
DecodeError can_decoder_run(const char* parsed_mmap_token,
						CanDatabaseModel model);

extern "C" {
#endif

CD_EXPORT DecodeError can_decoder_run(const char* parsed_mmap_token);

#ifdef __cplusplus
}
#endif
