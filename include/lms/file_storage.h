#pragma once

#include "lms/crypto.h"
#include "lms/errors.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <atomic>
#include <cstdint>
#include <limits>
#include <system_error>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <cerrno>
#endif

namespace lms::file_storage {
namespace fs = std::filesystem;
struct StorageError : RepositoryError { using RepositoryError::RepositoryError; };
struct CorruptData : StorageError { using StorageError::StorageError; };
inline constexpr std::size_t max_file_bytes = 64 * 1024 * 1024;
inline constexpr char magic[] = "LMSDAT01";
inline constexpr std::size_t header_size = 8 + 1 + 8 + 64;

inline bool exists(const fs::path& path) {
    std::error_code error;
    const bool found = fs::exists(path, error);
    if (error) throw StorageError("Cannot inspect storage file");
    return found;
}
inline std::string read_bytes(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) throw StorageError("Storage path is not a readable regular file");
    const auto size = fs::file_size(path, error);
    if (error || size > max_file_bytes) throw StorageError("Cannot read storage file or file exceeds 64 MiB limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw StorageError("Cannot open storage file");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size && !input.read(bytes.data(), static_cast<std::streamsize>(size)))
        throw StorageError("Cannot read complete storage file");
    if (input.peek() != std::char_traits<char>::eof() || input.bad())
        throw StorageError("Storage file changed while reading");
    return bytes;
}
inline std::string envelope(char kind, const std::string& payload) {
    if (payload.size() > max_file_bytes - header_size) throw StorageError("Storage snapshot exceeds size limit");
    std::string output(magic, 8);
    output += kind;
    const auto size = static_cast<std::uint64_t>(payload.size());
    for (int i = 0; i < 8; ++i) output += static_cast<char>((size >> (8*i)) & 255);
    output += crypto::sha256(payload);
    output += payload;
    return output;
}
inline std::string payload(char kind, const std::string& bytes) {
    if (bytes.empty()) throw CorruptData("Empty legacy storage file; explicit recovery required");
    if (bytes.compare(0, 8, magic, 8) != 0) return bytes; // Legacy format remains readable.
    if (bytes.size() < header_size || bytes[8] != kind) throw CorruptData("Invalid snapshot header");
    std::uint64_t size = 0;
    for (int i = 0; i < 8; ++i) size |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[9+i])) << (8*i);
    if (size != bytes.size() - header_size) throw CorruptData("Snapshot length mismatch");
    const auto body = bytes.substr(header_size);
    if (bytes.substr(17,64) != crypto::sha256(body)) throw CorruptData("Snapshot checksum mismatch");
    return body;
}

inline fs::path unique_sibling(const fs::path& path, const char* purpose) {
    static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    auto sibling = path;
    sibling += "." + std::string(purpose) + "-" + std::to_string(pid) + "-" + std::to_string(++sequence);
    return sibling;
}

// Exclusively create a new file. A stale file is never truncated or removed.
inline void write_new(const fs::path& path, const std::string& bytes) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw StorageError("Cannot create storage temporary file");
    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            const auto chunk = static_cast<DWORD>(bytes.size()-offset);
            if (!WriteFile(handle, bytes.data()+offset, chunk, &written, nullptr) || written == 0)
                throw StorageError("Cannot write complete storage snapshot");
            offset += written;
        }
        if (!FlushFileBuffers(handle)) throw StorageError("Cannot flush storage snapshot");
        const bool closed = CloseHandle(handle) != 0;
        handle = INVALID_HANDLE_VALUE;
        if (!closed) throw StorageError("Cannot close storage snapshot");
    } catch (...) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        std::error_code ignored; fs::remove(path,ignored);
        throw;
    }
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) throw StorageError("Cannot create storage temporary file");
    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::write(fd,bytes.data()+offset,bytes.size()-offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw StorageError("Cannot write complete storage snapshot");
            offset += static_cast<std::size_t>(n);
        }
        if (::fsync(fd) != 0) throw StorageError("Cannot flush storage snapshot");
        const int result = ::close(fd); fd = -1;
        if (result != 0) throw StorageError("Cannot close storage snapshot");
    } catch (...) {
        if (fd >= 0) ::close(fd);
        std::error_code ignored; fs::remove(path,ignored);
        throw;
    }
#endif
}
inline void replace(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw StorageError("Cannot replace storage snapshot");
#else
    if (::rename(temporary.c_str(), destination.c_str()) != 0) throw StorageError("Cannot replace storage snapshot");
#endif
}
inline void atomic_replace(const fs::path& destination, const std::string& bytes) {
    const auto temporary = unique_sibling(destination,"writing");
    write_new(temporary,bytes);
    try { replace(temporary,destination); }
    catch (...) { std::error_code ignored; fs::remove(temporary,ignored); throw; }
}
inline fs::path backup_path(const fs::path& path) { auto copy=path; copy += ".bak"; return copy; }
inline void save(const fs::path& path, char kind, const std::string& body) {
    // Prepare and sync new data before updating the previous-generation backup.
    const auto temporary = unique_sibling(path,"writing");
    write_new(temporary,envelope(kind,body));
    try {
        if (file_storage::exists(path)) atomic_replace(backup_path(path),read_bytes(path));
        replace(temporary,path);
    } catch (...) { std::error_code ignored; fs::remove(temporary,ignored); throw; }
}

// The lock file remains on disk; the OS releases its lock when the process exits.
// Recovery and a second server must not race the active file-backed server.
class DirectoryLock {
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
public:
    explicit DirectoryLock(const fs::path& directory) {
        const auto path = directory/".lms.lock";
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw StorageError("Data directory is in use or cannot be locked; stop the server before recovery");
#else
        fd_ = ::open(path.c_str(),O_RDWR | O_CREAT,0600);
        if (fd_ < 0) throw StorageError("Cannot open data directory lock");
        if (::flock(fd_,LOCK_EX | LOCK_NB) != 0) {
            ::close(fd_); fd_=-1;
            throw StorageError("Data directory is in use; stop the server before recovery");
        }
#endif
    }
    ~DirectoryLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) { ::flock(fd_,LOCK_UN); ::close(fd_); }
#endif
    }
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
};
} // namespace lms::file_storage
