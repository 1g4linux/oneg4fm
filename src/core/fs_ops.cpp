/*
 * POSIX-only filesystem helpers implementation (no Qt includes)
 * src/core/fs_ops.cpp
 */

#include "fs_ops.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <array>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <b3sum/blake3.h>

namespace PCManFM::FsOps {

// Forward declaration for use in helpers
bool ensure_parent_dirs(const std::string& path, Error& err);

namespace {

struct Fd {
    int fd;
    explicit Fd(int f = -1) : fd(f) {}
    ~Fd() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd(other.fd) { other.fd = -1; }
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) {
                ::close(fd);
            }
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
    bool valid() const { return fd >= 0; }
};

struct Dir {
    DIR* dir;
    explicit Dir(DIR* d = nullptr) : dir(d) {}
    ~Dir() {
        if (dir) {
            ::closedir(dir);
        }
    }
    Dir(const Dir&) = delete;
    Dir& operator=(const Dir&) = delete;
    Dir(Dir&& other) noexcept : dir(other.dir) { other.dir = nullptr; }
    Dir& operator=(Dir&& other) noexcept {
        if (this != &other) {
            if (dir) {
                ::closedir(dir);
            }
            dir = other.dir;
            other.dir = nullptr;
        }
        return *this;
    }
    bool valid() const { return dir != nullptr; }
};

inline void set_error(Error& err, const char* context) {
    err.code = errno;
    err.message = std::string(context) + ": " + std::strerror(errno);
}

std::string join_child_path(const std::string& parent, const char* child) {
    if (parent.empty()) {
        return std::string(child);
    }
    if (parent.back() == '/') {
        return parent + child;
    }
    return parent + "/" + child;
}

struct CopyJournalEntry {
    std::string path;
    bool isDir = false;
};

class CopyJournal {
   public:
    void recordFile(const std::string& path) { entries_.push_back(CopyJournalEntry{path, false}); }
    void recordDir(const std::string& path) { entries_.push_back(CopyJournalEntry{path, true}); }

    // Roll back only paths created by this operation.
    void rollback() {
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->isDir) {
                if (::rmdir(it->path.c_str()) < 0) {
                    if (errno == ENOENT || errno == ENOTEMPTY) {
                        continue;
                    }
                }
            }
            else {
                if (::unlink(it->path.c_str()) < 0) {
                    if (errno == ENOENT) {
                        continue;
                    }
                }
            }
        }
        entries_.clear();
    }

   private:
    std::vector<CopyJournalEntry> entries_;
};

bool blake3_file_impl(const std::string& path, std::string& hexHash, Error& err) {
    hexHash.clear();

    // Reject symlinks and non-regular files explicitly.
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0) {
        set_error(err, "lstat");
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        err.code = ELOOP;
        err.message = "symlinks are not supported for checksum calculation";
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        err.code = EINVAL;
        err.message = "not a regular file";
        return false;
    }

    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    Fd fd(::open(path.c_str(), flags));
    if (!fd.valid()) {
        set_error(err, "open");
        return false;
    }

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    std::array<char, 64 * 1024> buffer{};
    for (;;) {
        const ssize_t n = ::read(fd.fd, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "read");
            return false;
        }
        if (n == 0) {
            break;
        }
        blake3_hasher_update(&hasher, reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(n));
    }

    uint8_t out[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, out, BLAKE3_OUT_LEN);

    static const char* kHex = "0123456789abcdef";
    hexHash.resize(BLAKE3_OUT_LEN * 2);
    for (size_t i = 0; i < BLAKE3_OUT_LEN; ++i) {
        hexHash[2 * i] = kHex[(out[i] >> 4) & 0xF];
        hexHash[2 * i + 1] = kHex[out[i] & 0xF];
    }

    err = {};
    return true;
}

inline bool should_continue(const ProgressCallback& cb, const ProgressInfo& info) {
    if (!cb) {
        return true;
    }
    return cb(info);
}

bool read_all_fd(int fd, std::vector<std::uint8_t>& out, Error& err) {
    constexpr std::size_t chunk = 64 * 1024;
    std::vector<std::uint8_t> buffer;
    buffer.reserve(chunk);

    for (;;) {
        std::uint8_t tmp[chunk];
        const ssize_t n = ::read(fd, tmp, sizeof tmp);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "read");
            return false;
        }
        if (n == 0) {
            break;
        }
        buffer.insert(buffer.end(), tmp, tmp + n);
    }

    out.swap(buffer);
    return true;
}

bool write_all_fd(int fd, const std::uint8_t* data, std::size_t size, Error& err) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "write");
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

struct StatInfo {
    struct stat st{};
};

bool stat_at(int dirfd, const char* name, bool follow, StatInfo& out, Error& err) {
    int flags = follow ? 0 : AT_SYMLINK_NOFOLLOW;
    if (::fstatat(dirfd, name, &out.st, flags) < 0) {
        set_error(err, "fstatat");
        return false;
    }
    return true;
}

bool copy_symlink_at(int srcDir,
                     const char* srcName,
                     int dstDir,
                     const char* dstName,
                     const StatInfo& info,
                     Error& err,
                     const std::string& dstPath,
                     CopyJournal* journal,
                     bool preserveOwnership) {
    std::vector<char> buf(static_cast<std::size_t>(info.st.st_size) + 1);
    ssize_t len = ::readlinkat(srcDir, srcName, buf.data(), buf.size());
    if (len < 0) {
        set_error(err, "readlinkat");
        return false;
    }
    buf[static_cast<std::size_t>(len)] = '\0';
    if (::symlinkat(buf.data(), dstDir, dstName) < 0) {
        set_error(err, "symlinkat");
        return false;
    }
    if (journal) {
        journal->recordFile(dstPath);
    }
    // Preserve timestamps if possible
    struct timespec times[2];
    times[0] = info.st.st_atim;
    times[1] = info.st.st_mtim;
    ::utimensat(dstDir, dstName, times, AT_SYMLINK_NOFOLLOW);  // best effort
    if (preserveOwnership) {
        ::fchownat(dstDir, dstName, info.st.st_uid, info.st.st_gid, AT_SYMLINK_NOFOLLOW);  // best effort
    }
    return true;
}

bool copy_file_at(int srcDir,
                  const char* srcName,
                  int dstDir,
                  const char* dstName,
                  const StatInfo& info,
                  ProgressInfo& progress,
                  const ProgressCallback& cb,
                  Error& err,
                  const std::string& dstPath,
                  CopyJournal* journal,
                  bool preserveOwnership) {
    progress.bytesTotal += static_cast<std::uint64_t>(info.st.st_size);

    Fd in_fd(::openat(srcDir, srcName, O_RDONLY | O_CLOEXEC));
    if (!in_fd.valid()) {
        set_error(err, "openat");
        return false;
    }

    Fd out_fd(::openat(dstDir, dstName, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, info.st.st_mode & 0777));
    if (out_fd.valid()) {
        if (journal) {
            journal->recordFile(dstPath);
        }
    }
    else if (errno == EEXIST) {
        out_fd = Fd(::openat(dstDir, dstName, O_WRONLY | O_TRUNC | O_CLOEXEC));
        if (!out_fd.valid()) {
            set_error(err, "openat");
            return false;
        }
    }
    else {
        set_error(err, "openat");
        return false;
    }

    constexpr std::size_t chunk = 128 * 1024;  // a bit larger for throughput
    std::vector<std::uint8_t> buffer(chunk);

    for (;;) {
        const ssize_t n = ::read(in_fd.fd, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "read");
            return false;
        }
        if (n == 0) {
            break;
        }

        if (!write_all_fd(out_fd.fd, buffer.data(), static_cast<std::size_t>(n), err)) {
            return false;
        }

        progress.bytesDone += static_cast<std::uint64_t>(n);
        if (!should_continue(cb, progress)) {
            err.code = ECANCELED;
            err.message = "Cancelled";
            return false;
        }
    }

    struct timespec times[2];
    times[0] = info.st.st_atim;
    times[1] = info.st.st_mtim;
    ::futimens(out_fd.fd, times);  // best effort; ignore errors

    if (preserveOwnership) {
        ::fchown(out_fd.fd, info.st.st_uid, info.st.st_gid);  // best effort
    }
    ::fchmod(out_fd.fd, info.st.st_mode & 07777);  // best effort to match source mode, ignore umask

    if (::fsync(out_fd.fd) < 0) {
        set_error(err, "fsync");
        return false;
    }

    return true;
}

bool copy_dir_at(int srcDir,
                 const char* srcName,
                 int dstDir,
                 const char* dstName,
                 ProgressInfo& progress,
                 const ProgressCallback& cb,
                 Error& err,
                 int depth,
                 const std::string& dstPath,
                 CopyJournal* journal,
                 bool preserveOwnership);

bool copy_entry_at(int srcDir,
                   const char* srcName,
                   int dstDir,
                   const char* dstName,
                   ProgressInfo& progress,
                   const ProgressCallback& cb,
                   Error& err,
                   int depth,
                   const std::string& dstPath,
                   CopyJournal* journal,
                   bool preserveOwnership) {
    if (depth > kMaxRecursionDepth) {
        err.code = ELOOP;
        err.message = "Maximum recursion depth exceeded";
        return false;
    }

    StatInfo info;
    if (!stat_at(srcDir, srcName, /*follow=*/false, info, err)) {
        return false;
    }

    if (!should_continue(cb, progress)) {
        err.code = ECANCELED;
        err.message = "Cancelled";
        return false;
    }

    if (S_ISDIR(info.st.st_mode)) {
        return copy_dir_at(srcDir, srcName, dstDir, dstName, progress, cb, err, depth + 1, dstPath, journal,
                           preserveOwnership);
    }
    if (S_ISREG(info.st.st_mode)) {
        return copy_file_at(srcDir, srcName, dstDir, dstName, info, progress, cb, err, dstPath, journal,
                            preserveOwnership);
    }
    if (S_ISLNK(info.st.st_mode)) {
        return copy_symlink_at(srcDir, srcName, dstDir, dstName, info, err, dstPath, journal, preserveOwnership);
    }

    // Unsupported special file types
    err.code = ENOTSUP;
    err.message = "Unsupported file type";
    return false;
}

bool copy_dir_at(int srcDir,
                 const char* srcName,
                 int dstDir,
                 const char* dstName,
                 ProgressInfo& progress,
                 const ProgressCallback& cb,
                 Error& err,
                 int depth,
                 const std::string& dstPath,
                 CopyJournal* journal,
                 bool preserveOwnership) {
    StatInfo info;
    if (!stat_at(srcDir, srcName, /*follow=*/false, info, err)) {
        return false;
    }
    if (!S_ISDIR(info.st.st_mode)) {
        err.code = ENOTDIR;
        err.message = "Not a directory";
        return false;
    }

    // Create dest dir
    if (::mkdirat(dstDir, dstName, info.st.st_mode & 0777) < 0) {
        if (errno != EEXIST) {
            set_error(err, "mkdirat");
            return false;
        }
    }
    else {
        if (journal) {
            journal->recordDir(dstPath);
        }
    }

    // Open source and destination directories for recursion
    Fd newSrc(::openat(srcDir, srcName, O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!newSrc.valid()) {
        set_error(err, "openat");
        return false;
    }
    Fd newDst(::openat(dstDir, dstName, O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!newDst.valid()) {
        set_error(err, "openat");
        return false;
    }

    Dir dir(::fdopendir(newSrc.fd));
    if (!dir.valid()) {
        set_error(err, "fdopendir");
        return false;
    }
    // fd now owned by DIR; prevent double close
    newSrc.fd = -1;

    for (;;) {
        errno = 0;
        dirent* ent = ::readdir(dir.dir);
        if (!ent) {
            if (errno != 0) {
                set_error(err, "readdir");
                return false;
            }
            break;
        }
        const char* child = ent->d_name;
        if (!child || child[0] == '\0' || std::strcmp(child, ".") == 0 || std::strcmp(child, "..") == 0) {
            continue;
        }

        const std::string childDstPath = join_child_path(dstPath, child);
        if (!copy_entry_at(dirfd(dir.dir), child, newDst.fd, child, progress, cb, err, depth + 1, childDstPath, journal,
                           preserveOwnership)) {
            return false;
        }
    }

    // Preserve times best effort
    struct timespec times[2];
    times[0] = info.st.st_atim;
    times[1] = info.st.st_mtim;
    ::utimensat(dstDir, dstName, times, 0);
    if (preserveOwnership) {
        ::fchownat(dstDir, dstName, info.st.st_uid, info.st.st_gid, AT_SYMLINK_NOFOLLOW);
    }
    ::fchmodat(dstDir, dstName, info.st.st_mode & 07777, AT_SYMLINK_NOFOLLOW);

    return true;
}

bool delete_at(int dirfd, const char* name, ProgressInfo& progress, const ProgressCallback& cb, Error& err, int depth) {
    if (depth > kMaxRecursionDepth) {
        err.code = ELOOP;
        err.message = "Maximum recursion depth exceeded";
        return false;
    }

    StatInfo info;
    if (!stat_at(dirfd, name, /*follow=*/false, info, err)) {
        return false;
    }

    if (!should_continue(cb, progress)) {
        err.code = ECANCELED;
        err.message = "Cancelled";
        return false;
    }

    if (S_ISDIR(info.st.st_mode)) {
        Fd sub(::openat(dirfd, name, O_RDONLY | O_CLOEXEC | O_DIRECTORY));
        if (!sub.valid()) {
            set_error(err, "openat");
            return false;
        }
        Dir dir(::fdopendir(sub.fd));
        if (!dir.valid()) {
            set_error(err, "fdopendir");
            return false;
        }
        sub.fd = -1;

        for (;;) {
            errno = 0;
            dirent* ent = ::readdir(dir.dir);
            if (!ent) {
                if (errno != 0) {
                    set_error(err, "readdir");
                    return false;
                }
                break;
            }
            const char* child = ent->d_name;
            if (!child || child[0] == '\0' || std::strcmp(child, ".") == 0 || std::strcmp(child, "..") == 0) {
                continue;
            }
            if (!delete_at(::dirfd(dir.dir), child, progress, cb, err, depth + 1)) {
                return false;
            }
        }

        if (::unlinkat(dirfd, name, AT_REMOVEDIR) < 0) {
            set_error(err, "unlinkat");
            return false;
        }
    }
    else {
        if (::unlinkat(dirfd, name, 0) < 0) {
            set_error(err, "unlinkat");
            return false;
        }
    }

    progress.filesDone += 1;
    if (!should_continue(cb, progress)) {
        err.code = ECANCELED;
        err.message = "Cancelled";
        return false;
    }
    return true;
}

}  // namespace

bool blake3_file(const std::string& path, std::string& hexHash, Error& err) {
    return blake3_file_impl(path, hexHash, err);
}

bool read_file_all(const std::string& path, std::vector<std::uint8_t>& out, Error& err) {
    err = {};
    out.clear();
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.valid()) {
        set_error(err, "open");
        return false;
    }
    return read_all_fd(fd.fd, out, err);
}

bool write_file_atomic(const std::string& path, const std::uint8_t* data, std::size_t size, Error& err) {
    err = {};

    if (!ensure_parent_dirs(path, err)) {
        return false;
    }

    std::string tmpPath = path + ".XXXXXX";
    std::vector<char> tmpl(tmpPath.begin(), tmpPath.end());
    tmpl.push_back('\0');

    int tmpFd = ::mkstemp(tmpl.data());
    if (tmpFd < 0) {
        set_error(err, "mkstemp");
        return false;
    }

    Fd fd(tmpFd);

    if (!write_all_fd(fd.fd, data, size, err)) {
        ::unlink(tmpl.data());
        return false;
    }

    if (::fsync(fd.fd) < 0) {
        set_error(err, "fsync");
        ::unlink(tmpl.data());
        return false;
    }

    if (::rename(tmpl.data(), path.c_str()) < 0) {
        set_error(err, "rename");
        ::unlink(tmpl.data());
        return false;
    }

    return true;
}

bool make_dir_parents(const std::string& path, Error& err) {
    err = {};
    if (path.empty()) {
        return true;
    }

    // handle root or already existing path
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        err.code = ENOTDIR;
        err.message = "Not a directory: " + path;
        return false;
    }

    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        if (pos > 0) {  // skip leading slash
            const std::string parent = path.substr(0, pos);
            if (!make_dir_parents(parent, err)) {
                return false;
            }
        }
    }

    if (::mkdir(path.c_str(), 0777) < 0) {
        if (errno == EEXIST) {
            return true;
        }
        set_error(err, "mkdir");
        return false;
    }
    return true;
}

bool ensure_parent_dirs(const std::string& path, Error& err) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return true;
    }
    const std::string parent = path.substr(0, pos);
    if (parent.empty()) {
        return true;
    }
    return make_dir_parents(parent, err);
}

bool set_permissions(const std::string& path, unsigned int mode, Error& err) {
    err = {};
    if (::chmod(path.c_str(), static_cast<mode_t>(mode)) < 0) {
        set_error(err, "chmod");
        return false;
    }
    return true;
}

bool set_times(const std::string& path,
               std::int64_t atimeSec,
               std::int64_t atimeNsec,
               std::int64_t mtimeSec,
               std::int64_t mtimeNsec,
               Error& err) {
    err = {};
    struct timespec times[2];
    times[0].tv_sec = atimeSec;
    times[0].tv_nsec = atimeNsec;
    times[1].tv_sec = mtimeSec;
    times[1].tv_nsec = mtimeNsec;
    if (::utimensat(AT_FDCWD, path.c_str(), times, 0) < 0) {
        set_error(err, "utimensat");
        return false;
    }
    return true;
}

bool copy_path(const std::string& source,
               const std::string& destination,
               ProgressInfo& progress,
               const ProgressCallback& callback,
               Error& err,
               bool preserveOwnership) {
    err = {};

    // Ensure destination parent exists
    if (!ensure_parent_dirs(destination, err)) {
        return false;
    }

    auto splitPath = [](const std::string& path, std::string& parentOut, std::string& nameOut) {
        const auto pos = path.find_last_of('/');
        if (pos == std::string::npos) {
            parentOut = ".";
            nameOut = path;
        }
        else {
            parentOut = path.substr(0, pos);
            nameOut = path.substr(pos + 1);
            if (parentOut.empty()) {
                parentOut = ".";
            }
        }
    };

    std::string srcParent, srcName;
    splitPath(source, srcParent, srcName);
    std::string destParent, destName;
    splitPath(destination, destParent, destName);

    Fd srcParentFd(::open(srcParent.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!srcParentFd.valid()) {
        set_error(err, "open");
        return false;
    }

    StatInfo rootInfo;
    if (!stat_at(srcParentFd.fd, srcName.c_str(), /*follow=*/false, rootInfo, err)) {
        return false;
    }
    const bool srcIsDir = S_ISDIR(rootInfo.st.st_mode);

    Fd destParentFd(::open(destParent.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!destParentFd.valid()) {
        set_error(err, "open");
        return false;
    }

    CopyJournal journal;

    bool ok = false;
    if (srcIsDir) {
        ok = copy_dir_at(srcParentFd.fd, srcName.c_str(), destParentFd.fd, destName.c_str(), progress, callback, err, 0,
                         destination, &journal, preserveOwnership);
    }
    else if (S_ISREG(rootInfo.st.st_mode) || S_ISLNK(rootInfo.st.st_mode)) {
        ok = copy_entry_at(srcParentFd.fd, srcName.c_str(), destParentFd.fd, destName.c_str(), progress, callback, err,
                           0, destination, &journal, preserveOwnership);
    }
    else {
        err.code = ENOTSUP;
        err.message = "Unsupported file type";
        return false;
    }

    if (!ok) {
        journal.rollback();
    }

    if (ok) {
        progress.filesDone += 1;
    }
    return ok;
}

bool move_path(const std::string& source,
               const std::string& destination,
               ProgressInfo& progress,
               const ProgressCallback& callback,
               Error& err,
               bool forceCopyFallbackForTests,
               bool preserveOwnership) {
    err = {};

    if (!forceCopyFallbackForTests && ::rename(source.c_str(), destination.c_str()) == 0) {
        progress.filesDone += 1;
        progress.currentPath = source;
        should_continue(callback, progress);
        return true;
    }

    if (!forceCopyFallbackForTests && errno != EXDEV) {
        set_error(err, "rename");
        return false;
    }

    // Cross-device or forced fallback: copy then delete
    if (!copy_path(source, destination, progress, callback, err, preserveOwnership)) {
        return false;
    }

    if (!delete_path(source, progress, callback, err)) {
        // Keep copied destination content; never remove destination as recovery.
        return false;
    }

    return true;
}

bool delete_path(const std::string& path, ProgressInfo& progress, const ProgressCallback& callback, Error& err) {
    err = {};
    // Split path into parent/name
    const auto pos = path.find_last_of('/');
    std::string parent = (pos == std::string::npos) ? "." : path.substr(0, pos);
    std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);
    if (parent.empty()) {
        parent = ".";
    }

    Fd parentFd(::open(parent.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!parentFd.valid()) {
        set_error(err, "open");
        return false;
    }

    if (!delete_at(parentFd.fd, name.c_str(), progress, callback, err, 0)) {
        return false;
    }
    return true;
}

}  // namespace PCManFM::FsOps
