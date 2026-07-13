#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

/// NOTE: 
/* Instead of using the 
MMapHandle h;
mmap_open_ro(path, h);
...
mmap_close(h);

C++ RAII design would make MMapHandle responsible for its own lifetime.

std::unique_ptr only solves destruction. It doesn't know how to construct an mmap.
*/

enum class MMapAccess {
    ReadOnly,
    ReadWrite,
};


class MMapHandle {
public:
    MMapHandle(const std::filesystem::path& path, MMapAccess access);
    MMapHandle(const std::filesystem::path& path, std::size_t bytes);
    ~MMapHandle();

    MMapHandle(const MMapHandle&) = delete;
    MMapHandle& operator=(const MMapHandle&) = delete;

    MMapHandle(MMapHandle&& other) noexcept;
    MMapHandle& operator=(MMapHandle&& other) noexcept;

    // void open_ro(const std::filesystem::path& path);
    // void open_rw(const std::filesystem::path& path);
    // void create_rw(const std::filesystem::path& path, size_t size);

    // void close();

    [[nodiscard]] void* data() const noexcept { return addr; }
    [[nodiscard]] size_t bytes() const noexcept { return size; }
    [[nodiscard]] bool is_open() const noexcept { return addr != nullptr; }

public:
#ifdef _WIN32
    HANDLE file    = INVALID_HANDLE_VALUE;
    HANDLE mapping = NULL;
#else
    int    fd = -1;
#endif
    void*  addr = nullptr;
    size_t size = 0;
};


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
/// NOTE: Instead of calling this manually -> use inside the lifetime of the MMapHandle
void mmap_close(MMapHandle& h);

// Check whether a file exists at `path`.
bool mmap_file_exists(const char* path);


inline MMapHandle::~MMapHandle()
{
    mmap_close(*this);
}

inline MMapHandle::MMapHandle(const std::filesystem::path& path, MMapAccess access)
{
    if (access == MMapAccess::ReadOnly) {
        const std::string p = path.string();
        mmap_open_ro(p.c_str(), *this);
    } else {
        const std::string p = path.string();
        mmap_open_rw(p.c_str(), *this);
    }
}

inline MMapHandle::MMapHandle(const std::filesystem::path& path, std::size_t bytes)
{
    const std::string p = path.string();
    mmap_create_rw(p.c_str(), bytes, *this);
}

inline MMapHandle::MMapHandle(MMapHandle&& other) noexcept
{
#ifdef _WIN32
    file = other.file;
    mapping = other.mapping;
    other.file = INVALID_HANDLE_VALUE;
    other.mapping = NULL;
#else
    fd = other.fd;
    other.fd = -1;
#endif
    addr = other.addr;
    size = other.size;

    other.addr = nullptr;
    other.size = 0;
}

inline MMapHandle& MMapHandle::operator=(MMapHandle&& other) noexcept
{
    if (this != &other) {
    mmap_close(*this);

#ifdef _WIN32
    file = other.file;
    mapping = other.mapping;
    other.file = INVALID_HANDLE_VALUE;
    other.mapping = NULL;
#else
    fd = other.fd;
    other.fd = -1;
#endif
    addr = other.addr;
    size = other.size;

    other.addr = nullptr;
    other.size = 0;
    }

    return *this;
}

// void MMapHandle::open_rw(const std::filesystem::path& path)
// {
//     const std::string p = path.string();
//     mmap_open_rw(p.c_str(), *this);
// }

// void MMapHandle::create_rw(const std::filesystem::path& path,
//                            std::size_t bytes)
// {
//     const std::string p = path.string();
//     mmap_create_rw(p.c_str(), bytes, *this);
// }