#include "mmap_wrapper_RAII.h"

#include <string>

#ifdef _WIN32
// ═══════════════════════════════════════════════════════════════════════════
// Windows implementation
// ═══════════════════════════════════════════════════════════════════════════

namespace {

[[noreturn]] void throw_mmap_error(const char* op, const char* path, const std::string& detail) {
    std::string msg = std::string(op) + " failed path='" + (path ? path : "<null>") + "'";
    if (!detail.empty()) {
        msg += " detail=" + detail;
    }
    throw MMapError(msg);
}

std::string win_last_error() {
    return "GetLastError=" + std::to_string(static_cast<unsigned long long>(GetLastError()));
}

} // namespace

void mmap_open_ro(const char* path, MMapHandle& out) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        throw_mmap_error("CreateFileA(ro)", path, win_last_error());
    }

    LARGE_INTEGER fsize;
    if (!GetFileSizeEx(file, &fsize) || fsize.QuadPart == 0) {
        CloseHandle(file);
        throw_mmap_error("GetFileSizeEx(ro)", path, win_last_error());
    }

    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!mapping) {
        CloseHandle(file);
        throw_mmap_error("CreateFileMappingA(ro)", path, win_last_error());
    }

    void* addr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!addr) {
        CloseHandle(mapping);
        CloseHandle(file);
        throw_mmap_error("MapViewOfFile(ro)", path, win_last_error());
    }

    out.file    = file;
    out.mapping = mapping;
    out.addr    = addr;
    out.size    = static_cast<size_t>(fsize.QuadPart);
}

void mmap_open_rw(const char* path, MMapHandle& out) {
    HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        throw_mmap_error("CreateFileA(rw)", path, win_last_error());
    }

    LARGE_INTEGER fsize;
    if (!GetFileSizeEx(file, &fsize) || fsize.QuadPart == 0) {
        CloseHandle(file);
        throw_mmap_error("GetFileSizeEx(rw)", path, win_last_error());
    }

    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (!mapping) {
        CloseHandle(file);
        throw_mmap_error("CreateFileMappingA(rw)", path, win_last_error());
    }

    void* addr = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!addr) {
        CloseHandle(mapping);
        CloseHandle(file);
        throw_mmap_error("MapViewOfFile(rw)", path, win_last_error());
    }

    out.file    = file;
    out.mapping = mapping;
    out.addr    = addr;
    out.size    = static_cast<size_t>(fsize.QuadPart);
}

void mmap_create_rw(const char* path, size_t size, MMapHandle& out) {
    if (size == 0) {
        throw_mmap_error("mmap_create_rw", path, "size=0");
    }

    HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        throw_mmap_error("CreateFileA(create_rw)", path, win_last_error());
    }

    // Extend the file to the requested size.
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(file, li, NULL, FILE_BEGIN) || !SetEndOfFile(file)) {
        CloseHandle(file);
        throw_mmap_error("SetEndOfFile(create_rw)", path, win_last_error());
    }

    DWORD hi = static_cast<DWORD>(size >> 32);
    DWORD lo = static_cast<DWORD>(size & 0xFFFFFFFF);
    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READWRITE, hi, lo, NULL);
    if (!mapping) {
        CloseHandle(file);
        throw_mmap_error("CreateFileMappingA(create_rw)", path, win_last_error());
    }

    void* addr = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!addr) {
        CloseHandle(mapping);
        CloseHandle(file);
        throw_mmap_error("MapViewOfFile(create_rw)", path, win_last_error());
    }

    out.file    = file;
    out.mapping = mapping;
    out.addr    = addr;
    out.size    = size;
}

void mmap_close(MMapHandle& h) {
    if (h.addr)                          { UnmapViewOfFile(h.addr); h.addr = nullptr; }
    if (h.mapping)                       { CloseHandle(h.mapping);  h.mapping = NULL; }
    if (h.file != INVALID_HANDLE_VALUE)  { CloseHandle(h.file);    h.file = INVALID_HANDLE_VALUE; }
    h.size = 0;
}

bool mmap_file_exists(const char* path) {
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

#else
// ═══════════════════════════════════════════════════════════════════════════
// POSIX (Linux / macOS) implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

/* 20260705
If every exit path does mmap_close(handle) before throwing or returning, then you're fine.

The reason many C++ developers recommend RAII is not because your approach is wrong—it's because it's easy to accidentally write:

mmap_open_rw(path, handle);

// 200 lines later...

throw std::runtime_error(...);   // Oops, forgot mmap_close(handle)
and now you've leaked the mapping.
RAII eliminates that class of bug automatically.
*/

namespace {

[[noreturn]] void throw_mmap_error(const char* op, const char* path, int err) {
    std::string msg = std::string(op) + " failed path='" + (path ? path : "<null>")
        + "' errno=" + std::to_string(err) + " (" + std::strerror(err) + ")";
    throw MMapError(msg);
}

} // namespace

void mmap_open_ro(const char* path, MMapHandle& out) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        throw_mmap_error("open(ro)", path, errno);
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        const int err = errno;
        ::close(fd);
        throw_mmap_error("fstat(ro)", path, err);
    }

    void* addr = mmap(nullptr, static_cast<size_t>(st.st_size),
                       PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        const int err = errno;
        ::close(fd);
        throw_mmap_error("mmap(ro)", path, err);
    }

    out.fd   = fd;
    out.addr = addr;
    out.size = static_cast<size_t>(st.st_size);
}

void mmap_open_rw(const char* path, MMapHandle& out) {
    int fd = ::open(path, O_RDWR);
    if (fd < 0) {
        throw_mmap_error("open(rw)", path, errno);
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        const int err = errno;
        ::close(fd);
        throw_mmap_error("fstat(rw)", path, err);
    }

    void* addr = mmap(nullptr, static_cast<size_t>(st.st_size),
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        const int err = errno;
        ::close(fd);
        throw_mmap_error("mmap(rw)", path, err);
    }

    out.fd   = fd;
    out.addr = addr;
    out.size = static_cast<size_t>(st.st_size);
}

void mmap_create_rw(const char* path, size_t size, MMapHandle& out) {
    if (size == 0) {
        throw MMapError(std::string("mmap_create_rw failed path='") + (path ? path : "<null>") + "' size=0");
    }

    int fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        throw_mmap_error("open(create_rw)", path, errno);
    }

    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const int err = errno;
        ::close(fd);
        throw_mmap_error("ftruncate(create_rw)", path, err);
    }

    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        const int err = errno;
        ::close(fd);
        throw_mmap_error("mmap(create_rw)", path, err);
    }

    out.fd   = fd;
    out.addr = addr;
    out.size = size;
}

/// NOTE: user pass the the close handle, function check the null at one place then call system call to avoid failed
void mmap_close(MMapHandle& h) {
    if (h.addr) { munmap(h.addr, h.size); h.addr = nullptr; }
    if (h.fd >= 0) { ::close(h.fd); h.fd = -1; }
    h.size = 0;
}

bool mmap_file_exists(const char* path) {
    return ::access(path, F_OK) == 0;
}

#endif
