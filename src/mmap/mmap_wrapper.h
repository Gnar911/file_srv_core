#pragma once

#include <cstddef>
#include <stdexcept>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

struct MMapHandle {
    HANDLE file    = INVALID_HANDLE_VALUE;
    HANDLE mapping = NULL;
    void*  addr    = nullptr;
    size_t size    = 0;
};

#else

struct MMapHandle {
    int    fd   = -1;
    void*  addr = nullptr;
    size_t size = 0;
};

#endif

class MMapError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Open an existing file read-only (private mapping).
// Throws MMapError on failure.
void mmap_open_ro(const char* path, MMapHandle& out);

// Open an existing file read-write (shared mapping).
// Throws MMapError on failure.
void mmap_open_rw(const char* path, MMapHandle& out);

// Create (or truncate) a file to `size` bytes and map it read-write (shared).
// Throws MMapError on failure.
void mmap_create_rw(const char* path, size_t size, MMapHandle& out);

// Unmap and close all handles; resets `h` to default state.
void mmap_close(MMapHandle& h);

// Check whether a file exists at `path`.
bool mmap_file_exists(const char* path);
