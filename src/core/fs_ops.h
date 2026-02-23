/*
 * POSIX-only filesystem helpers (no Qt includes)
 * src/core/fs_ops.h
 */

#ifndef PCMANFM_FS_OPS_H
#define PCMANFM_FS_OPS_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Oneg4FM::FsOps {

// Maximum recursion depth to avoid runaway traversal (symlink loops, pathological trees).
constexpr int kMaxRecursionDepth = 256;

struct Error {
    int code = 0;
    std::string message;

    bool isSet() const { return code != 0 || !message.empty(); }
};

struct ProgressInfo {
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    int filesDone = 0;
    int filesTotal = 0;
    std::string currentPath;
};

using ProgressCallback = std::function<bool(const ProgressInfo&)>;

bool read_file_all(const std::string& path, std::vector<std::uint8_t>& out, Error& err);
bool write_file_atomic(const std::string& path, const std::uint8_t* data, std::size_t size, Error& err);
bool make_dir_parents(const std::string& path, Error& err);
bool set_permissions(const std::string& path, unsigned int mode, Error& err);
bool set_times(const std::string& path,
               std::int64_t atimeSec,
               std::int64_t atimeNsec,
               std::int64_t mtimeSec,
               std::int64_t mtimeNsec,
               Error& err);

// Core operations; callbacks may return false to request cancellation.
bool copy_path(const std::string& source,
               const std::string& destination,
               ProgressInfo& progress,
               const ProgressCallback& callback,
               Error& err,
               bool preserveOwnership = false,
               bool overwriteExisting = true);

bool move_path(const std::string& source,
               const std::string& destination,
               ProgressInfo& progress,
               const ProgressCallback& callback,
               Error& err,
               bool forceCopyFallbackForTests = false,
               bool preserveOwnership = false,
               bool overwriteExisting = true);

bool delete_path(const std::string& path, ProgressInfo& progress, const ProgressCallback& callback, Error& err);

// Compute a BLAKE3 checksum for a regular file (rejects symlinks and non-regular files).
bool blake3_file(const std::string& path, std::string& hexHash, Error& err);

}  // namespace Oneg4FM::FsOps

#endif  // PCMANFM_FS_OPS_H
