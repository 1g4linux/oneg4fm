/*
 * POSIX-only filesystem helpers implementation (no Qt includes)
 * src/core/fs_ops.cpp
 */

#include "fs_ops.h"
#include "linux_fs_safety.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <array>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <b3sum/blake3.h>

namespace PCManFM::FsOps {

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

constexpr std::uint64_t kResolveNoSymlinks = LinuxFsSafety::kResolveNoSymlinks;
constexpr std::uint64_t kResolveNoMagiclinks = LinuxFsSafety::kResolveNoMagiclinks;

bool is_dot_or_dotdot(const char* name) {
    return name && (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0);
}

void split_path_components(const std::string& path, bool& absolute, std::vector<std::string>& components) {
    components.clear();
    absolute = !path.empty() && path.front() == '/';

    std::size_t i = absolute ? 1 : 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        if (i >= path.size()) {
            break;
        }
        std::size_t j = i;
        while (j < path.size() && path[j] != '/') {
            ++j;
        }
        components.emplace_back(path.substr(i, j - i));
        i = j;
    }
}

int open_dir_flags() {
    return LinuxFsSafety::open_dir_flags();
}

bool duplicate_fd(int fd, Fd& out, Error& err) {
    Fd dupFd(::dup(fd));
    if (!dupFd.valid()) {
        set_error(err, "dup");
        return false;
    }
    out = std::move(dupFd);
    return true;
}

bool open_under_fd(int dirfd, const char* path, int flags, mode_t mode, std::uint64_t resolve, Fd& out, Error& err) {
    LinuxFsSafety::Fd safeFd;
    if (!LinuxFsSafety::open_under(dirfd, path, flags, mode, resolve, safeFd, err)) {
        return false;
    }
    out = Fd(safeFd.release());
    return true;
}

bool open_anchor_dir(bool absolute, Fd& out, Error& err) {
    const char* root = absolute ? "/" : ".";
    Fd base(::open(root, open_dir_flags()));
    if (!base.valid()) {
        set_error(err, "open");
        return false;
    }
    out = std::move(base);
    return true;
}

bool open_dir_nofollow_at(int parentFd, const char* name, Fd& out, Error& err) {
    return open_under_fd(parentFd, name, open_dir_flags(), 0, kResolveNoSymlinks, out, err);
}

bool open_dir_path_secure(const std::string& path, bool createMissing, Fd& out, Error& err) {
    bool absolute = false;
    std::vector<std::string> components;
    split_path_components(path, absolute, components);

    Fd current;
    if (!open_anchor_dir(absolute, current, err)) {
        return false;
    }

    for (const auto& comp : components) {
        if (comp.empty() || comp == ".") {
            continue;
        }

        if (createMissing && !LinuxFsSafety::mkdir_under(current.fd, comp, 0777, err, true)) {
            return false;
        }

        Fd next;
        if (!open_dir_nofollow_at(current.fd, comp.c_str(), next, err)) {
            return false;
        }
        current = std::move(next);
    }

    out = std::move(current);
    return true;
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

void split_parent_and_name(const std::string& path, std::string& parentOut, std::string& nameOut) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        parentOut = ".";
        nameOut = path;
        return;
    }

    parentOut = path.substr(0, pos);
    nameOut = path.substr(pos + 1);
    if (parentOut.empty()) {
        parentOut = ".";
    }
}

bool write_all_fd(int fd, const std::uint8_t* data, std::size_t size, Error& err);

struct CopyJournalEntry {
    std::string relPath;
    enum class Kind : std::uint8_t {
        File = 0,
        Directory = 1,
        Staging = 2,
    };
    Kind kind = Kind::File;
};

class CopyJournal {
   public:
    bool init(int rootFd, Error& err) {
        maxInMemoryEntries_ = read_max_in_memory_entries();
        return duplicate_fd(rootFd, rootFd_, err);
    }

    bool recordFile(const std::string& relPath, Error& err) {
        return record(CopyJournalEntry::Kind::File, relPath, err);
    }
    bool recordDir(const std::string& relPath, Error& err) {
        return record(CopyJournalEntry::Kind::Directory, relPath, err);
    }
    bool recordStaging(const std::string& relPath, Error& err) {
        return record(CopyJournalEntry::Kind::Staging, relPath, err);
    }

    // Roll back only paths created by this operation.
    void rollback() {
        if (!rootFd_.valid()) {
            return;
        }

        for (auto it = memoryEntries_.rbegin(); it != memoryEntries_.rend(); ++it) {
            rollback_entry(*it);
        }

        for (auto it = spillOffsets_.rbegin(); it != spillOffsets_.rend(); ++it) {
            CopyJournalEntry spilled;
            Error readErr;
            if (!read_spill_entry(*it, spilled, readErr)) {
                continue;
            }
            rollback_entry(spilled);
        }

        clear();
    }

    void clear() {
        memoryEntries_.clear();
        spillOffsets_.clear();
        spillFd_ = Fd();
    }

   private:
    static constexpr std::size_t kDefaultMaxInMemoryEntries = 1024;
    static constexpr const char* kJournalMaxEntriesEnv = "PCMANFM_COPY_JOURNAL_MAX_IN_MEMORY_ENTRIES";

    static std::size_t read_max_in_memory_entries() {
        const char* raw = std::getenv(kJournalMaxEntriesEnv);
        if (!raw || raw[0] == '\0') {
            return kDefaultMaxInMemoryEntries;
        }

        errno = 0;
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(raw, &end, 10);
        if (errno != 0 || end == raw || (end && *end != '\0')) {
            return kDefaultMaxInMemoryEntries;
        }
        if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
            return std::numeric_limits<std::size_t>::max();
        }
        return static_cast<std::size_t>(parsed);
    }

    static std::string spill_file_name(unsigned int attempt) {
        return std::string(".pcmanfm.copy-journal.") + std::to_string(::getpid()) + "." + std::to_string(attempt);
    }

    static bool read_exact_at(int fd, std::uint64_t offset, std::uint8_t* out, std::size_t size, Error& err) {
        std::size_t total = 0;
        while (total < size) {
            const ssize_t n =
                ::pread(fd, out + total, size - total, static_cast<off_t>(offset + static_cast<std::uint64_t>(total)));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                set_error(err, "pread");
                return false;
            }
            if (n == 0) {
                err.code = EIO;
                err.message = "Short read from journal spill file";
                return false;
            }
            total += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool ensure_spill_file(Error& err) {
        if (spillDisabled_) {
            return false;
        }
        if (spillFd_.valid()) {
            return true;
        }

        int flags = O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        for (unsigned int attempt = 0; attempt < 256; ++attempt) {
            const std::string candidate = spill_file_name(attempt);
            Fd newSpillFd;
            if (open_under_fd(rootFd_.fd, candidate.c_str(), flags, 0600, kResolveNoSymlinks, newSpillFd, err)) {
                if (::unlinkat(rootFd_.fd, candidate.c_str(), 0) != 0) {
                    set_error(err, "unlinkat");
                    return false;
                }
                spillFd_ = std::move(newSpillFd);
                return true;
            }
            if (err.code == EEXIST) {
                err = {};
                continue;
            }
            return false;
        }

        err.code = EEXIST;
        err.message = "Failed to allocate copy journal spill file";
        return false;
    }

    bool append_spill_entry(const CopyJournalEntry& entry, Error& err) {
        if (!ensure_spill_file(err)) {
            return false;
        }

        if (entry.relPath.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            err.code = ENAMETOOLONG;
            err.message = "Journal path entry is too long";
            return false;
        }

        const off_t offset = ::lseek(spillFd_.fd, 0, SEEK_END);
        if (offset < 0) {
            set_error(err, "lseek");
            return false;
        }

        const std::uint32_t pathLen = static_cast<std::uint32_t>(entry.relPath.size());
        std::array<std::uint8_t, 5> header{};
        header[0] = static_cast<std::uint8_t>(entry.kind);
        header[1] = static_cast<std::uint8_t>(pathLen & 0xffU);
        header[2] = static_cast<std::uint8_t>((pathLen >> 8U) & 0xffU);
        header[3] = static_cast<std::uint8_t>((pathLen >> 16U) & 0xffU);
        header[4] = static_cast<std::uint8_t>((pathLen >> 24U) & 0xffU);

        if (!write_all_fd(spillFd_.fd, header.data(), header.size(), err)) {
            return false;
        }
        if (pathLen > 0 &&
            !write_all_fd(spillFd_.fd, reinterpret_cast<const std::uint8_t*>(entry.relPath.data()), pathLen, err)) {
            return false;
        }

        spillOffsets_.push_back(static_cast<std::uint64_t>(offset));
        return true;
    }

    bool read_spill_entry(std::uint64_t offset, CopyJournalEntry& out, Error& err) const {
        if (!spillFd_.valid()) {
            err.code = EBADF;
            err.message = "Copy journal spill file is not open";
            return false;
        }

        std::array<std::uint8_t, 5> header{};
        if (!read_exact_at(spillFd_.fd, offset, header.data(), header.size(), err)) {
            return false;
        }

        switch (header[0]) {
            case static_cast<std::uint8_t>(CopyJournalEntry::Kind::File):
                out.kind = CopyJournalEntry::Kind::File;
                break;
            case static_cast<std::uint8_t>(CopyJournalEntry::Kind::Directory):
                out.kind = CopyJournalEntry::Kind::Directory;
                break;
            case static_cast<std::uint8_t>(CopyJournalEntry::Kind::Staging):
                out.kind = CopyJournalEntry::Kind::Staging;
                break;
            default:
                err.code = EINVAL;
                err.message = "Invalid journal spill entry kind";
                return false;
        }

        const std::uint32_t pathLen =
            static_cast<std::uint32_t>(header[1]) | (static_cast<std::uint32_t>(header[2]) << 8U) |
            (static_cast<std::uint32_t>(header[3]) << 16U) | (static_cast<std::uint32_t>(header[4]) << 24U);
        out.relPath.assign(pathLen, '\0');
        if (pathLen == 0) {
            return true;
        }
        return read_exact_at(spillFd_.fd, offset + header.size(), reinterpret_cast<std::uint8_t*>(out.relPath.data()),
                             pathLen, err);
    }

    bool maybe_spill(Error& err) {
        if (spillDisabled_) {
            return true;
        }
        if (memoryEntries_.size() <= maxInMemoryEntries_) {
            return true;
        }

        const std::size_t retain = (maxInMemoryEntries_ == 0) ? 0 : (maxInMemoryEntries_ / 2);
        while (memoryEntries_.size() > retain) {
            Error spillErr;
            if (!append_spill_entry(memoryEntries_.front(), spillErr)) {
                spillDisabled_ = true;
                err = {};
                return true;
            }
            memoryEntries_.pop_front();
        }
        return true;
    }

    bool record(CopyJournalEntry::Kind kind, const std::string& relPath, Error& err) {
        memoryEntries_.push_back(CopyJournalEntry{relPath, kind});
        return maybe_spill(err);
    }

    void rollback_entry(const CopyJournalEntry& entry) {
        Error cleanupErr;
        if (entry.kind == CopyJournalEntry::Kind::Directory) {
            LinuxFsSafety::rmdir_under(rootFd_.fd, entry.relPath, cleanupErr, true, true);
            return;
        }
        LinuxFsSafety::unlink_under(rootFd_.fd, entry.relPath, cleanupErr, true);
    }

    Fd rootFd_;
    Fd spillFd_;
    std::deque<CopyJournalEntry> memoryEntries_;
    std::vector<std::uint64_t> spillOffsets_;
    std::size_t maxInMemoryEntries_ = kDefaultMaxInMemoryEntries;
    bool spillDisabled_ = false;
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

enum class SourceEntryType {
    Directory,
    RegularFile,
    Symlink,
};

bool open_source_entry_for_copy(int srcDir,
                                const char* srcName,
                                Fd& outFd,
                                StatInfo& outInfo,
                                SourceEntryType& outType,
                                Error& err) {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    Fd fd;
    if (open_under_fd(srcDir, srcName, flags, 0, kResolveNoSymlinks, fd, err)) {
        if (::fstat(fd.fd, &outInfo.st) < 0) {
            set_error(err, "fstat");
            return false;
        }

        if (S_ISDIR(outInfo.st.st_mode)) {
            outType = SourceEntryType::Directory;
            outFd = std::move(fd);
            return true;
        }
        if (S_ISREG(outInfo.st.st_mode)) {
            outType = SourceEntryType::RegularFile;
            outFd = std::move(fd);
            return true;
        }

        err.code = ENOTSUP;
        err.message = "Unsupported file type";
        return false;
    }

    if (err.code != ELOOP) {
        return false;
    }
    err = {};

    int linkFlags = O_PATH | O_CLOEXEC;
#ifdef O_NOFOLLOW
    linkFlags |= O_NOFOLLOW;
#endif
    Fd linkFd;
    if (!open_under_fd(srcDir, srcName, linkFlags, 0, kResolveNoMagiclinks, linkFd, err)) {
        return false;
    }

    if (::fstat(linkFd.fd, &outInfo.st) < 0) {
        set_error(err, "fstat");
        return false;
    }
    if (!S_ISLNK(outInfo.st.st_mode)) {
        err.code = ELOOP;
        err.message = "Path resolved through a symlink";
        return false;
    }

    outType = SourceEntryType::Symlink;
    outFd = std::move(linkFd);
    return true;
}

bool read_symlink_target(int symlinkFd, int srcDir, const char* srcName, std::string& outTarget, Error& err) {
    std::size_t cap = 256;
    while (cap <= (1U << 20)) {
        std::vector<char> buf(cap);
        const ssize_t len = ::readlinkat(symlinkFd, "", buf.data(), buf.size());
        if (len >= 0) {
            if (static_cast<std::size_t>(len) < buf.size()) {
                outTarget.assign(buf.data(), static_cast<std::size_t>(len));
                return true;
            }
            cap *= 2;
            continue;
        }
        if (errno != EINVAL && errno != ENOENT) {
            set_error(err, "readlinkat");
            return false;
        }
        break;
    }

    std::vector<char> buf(256);
    while (buf.size() <= (1U << 20)) {
        const ssize_t len = ::readlinkat(srcDir, srcName, buf.data(), buf.size());
        if (len < 0) {
            set_error(err, "readlinkat");
            return false;
        }
        if (static_cast<std::size_t>(len) < buf.size()) {
            outTarget.assign(buf.data(), static_cast<std::size_t>(len));
            return true;
        }
        buf.resize(buf.size() * 2);
    }

    err.code = ENAMETOOLONG;
    err.message = "Symlink target too long";
    return false;
}

std::string parent_rel_path(const std::string& relPath) {
    const auto pos = relPath.find_last_of('/');
    if (pos == std::string::npos) {
        return std::string();
    }
    return relPath.substr(0, pos);
}

std::string staging_rel_path_for_target(const std::string& targetPath, const std::string& stagingName) {
    const std::string parent = parent_rel_path(targetPath);
    if (parent.empty()) {
        return stagingName;
    }
    return join_child_path(parent, stagingName.c_str());
}

std::string build_staging_file_name(unsigned int attempt) {
    return std::string(".pcmanfm.copy-stage.") + std::to_string(::getpid()) + "." + std::to_string(attempt);
}

bool create_staging_file_under(int dstDir, mode_t mode, Fd& outFd, std::string& outName, Error& err) {
    int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    for (unsigned int attempt = 0; attempt < 256; ++attempt) {
        const std::string candidate = build_staging_file_name(attempt);
        Fd fd;
        if (open_under_fd(dstDir, candidate.c_str(), flags, mode, kResolveNoSymlinks, fd, err)) {
            outFd = std::move(fd);
            outName = candidate;
            return true;
        }
        if (err.code == EEXIST) {
            err = {};
            continue;
        }
        return false;
    }

    err.code = EEXIST;
    err.message = "Failed to allocate staging file name";
    return false;
}

bool rename_staging_into_destination(int dstDir,
                                     const std::string& stagingName,
                                     const char* dstName,
                                     bool overwriteExisting,
                                     bool& destinationCreated,
                                     Error& err) {
#ifdef RENAME_NOREPLACE
    if (LinuxFsSafety::rename_under(dstDir, stagingName, dstDir, dstName, static_cast<unsigned int>(RENAME_NOREPLACE),
                                    err)) {
        destinationCreated = true;
        return true;
    }
    if (err.code != EEXIST) {
        return false;
    }
#else
    err.code = ENOSYS;
    err.message = "renameat2 RENAME_NOREPLACE is required but unavailable on this kernel";
    return false;
#endif

    if (!overwriteExisting) {
        return false;
    }
    err = {};

    int probeFlags = O_PATH | O_CLOEXEC;
#ifdef O_NOFOLLOW
    probeFlags |= O_NOFOLLOW;
#endif

    Fd probeFd;
    if (!open_under_fd(dstDir, dstName, probeFlags, 0, kResolveNoSymlinks, probeFd, err)) {
#ifdef RENAME_NOREPLACE
        if (err.code == ENOENT) {
            err = {};
            if (LinuxFsSafety::rename_under(dstDir, stagingName, dstDir, dstName,
                                            static_cast<unsigned int>(RENAME_NOREPLACE), err)) {
                destinationCreated = true;
                return true;
            }
        }
#endif
        return false;
    }

    struct stat dstInfo{};
    if (::fstat(probeFd.fd, &dstInfo) < 0) {
        set_error(err, "fstat");
        return false;
    }
    if (!S_ISREG(dstInfo.st_mode)) {
        err.code = EISDIR;
        err.message = "Destination exists and is not a regular file";
        return false;
    }

    if (!LinuxFsSafety::rename_under(dstDir, stagingName, dstDir, dstName, 0, err)) {
        return false;
    }

    destinationCreated = false;
    return true;
}

bool copy_symlink_at(int srcDir,
                     const char* srcName,
                     int srcLinkFd,
                     int dstDir,
                     const char* dstName,
                     const StatInfo& info,
                     Error& err,
                     const std::string& dstPath,
                     CopyJournal* journal,
                     bool preserveOwnership,
                     bool overwriteExisting) {
    std::string target;
    if (!read_symlink_target(srcLinkFd, srcDir, srcName, target, err)) {
        return false;
    }

    bool createdNewPath = false;
    if (::symlinkat(target.c_str(), dstDir, dstName) == 0) {
        createdNewPath = true;
    }
    else {
        if (errno != EEXIST || !overwriteExisting) {
            set_error(err, "symlinkat");
            return false;
        }

        if (::unlinkat(dstDir, dstName, 0) < 0) {
            set_error(err, "unlinkat");
            return false;
        }
        if (::symlinkat(target.c_str(), dstDir, dstName) < 0) {
            set_error(err, "symlinkat");
            return false;
        }
    }

    if (createdNewPath && journal) {
        if (!journal->recordFile(dstPath, err)) {
            return false;
        }
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

bool copy_file_at(int srcFd,
                  int dstDir,
                  const char* dstName,
                  const StatInfo& info,
                  ProgressInfo& progress,
                  const ProgressCallback& cb,
                  Error& err,
                  const std::string& dstPath,
                  CopyJournal* journal,
                  bool preserveOwnership,
                  bool overwriteExisting) {
    progress.bytesTotal += static_cast<std::uint64_t>(info.st.st_size);

    Fd in_fd;
    if (!duplicate_fd(srcFd, in_fd, err)) {
        return false;
    }
    if (::lseek(in_fd.fd, 0, SEEK_SET) < 0 && errno != ESPIPE) {
        set_error(err, "lseek");
        return false;
    }

    Fd stagingFd;
    std::string stagingName;
    if (!create_staging_file_under(dstDir, info.st.st_mode & 0777, stagingFd, stagingName, err)) {
        return false;
    }

    const std::string stagingJournalPath = staging_rel_path_for_target(dstPath, stagingName);
    bool stagingRecorded = false;
    if (journal) {
        if (!journal->recordStaging(stagingJournalPath, err)) {
            Error cleanupErr;
            LinuxFsSafety::unlink_under(dstDir, stagingName, cleanupErr, true);
            return false;
        }
        stagingRecorded = true;
    }

    const auto cleanup_staging_if_untracked = [&]() {
        if (journal && stagingRecorded) {
            return;
        }
        Error cleanupErr;
        LinuxFsSafety::unlink_under(dstDir, stagingName, cleanupErr, true);
    };

    constexpr std::size_t chunk = 128 * 1024;  // a bit larger for throughput
    std::vector<std::uint8_t> buffer(chunk);

    for (;;) {
        const ssize_t n = ::read(in_fd.fd, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "read");
            cleanup_staging_if_untracked();
            return false;
        }
        if (n == 0) {
            break;
        }

        if (!write_all_fd(stagingFd.fd, buffer.data(), static_cast<std::size_t>(n), err)) {
            cleanup_staging_if_untracked();
            return false;
        }

        progress.bytesDone += static_cast<std::uint64_t>(n);
        if (!should_continue(cb, progress)) {
            err.code = ECANCELED;
            err.message = "Cancelled";
            cleanup_staging_if_untracked();
            return false;
        }
    }

    struct timespec times[2];
    times[0] = info.st.st_atim;
    times[1] = info.st.st_mtim;
    ::futimens(stagingFd.fd, times);  // best effort; ignore errors

    if (preserveOwnership) {
        ::fchown(stagingFd.fd, info.st.st_uid, info.st.st_gid);  // best effort
    }
    ::fchmod(stagingFd.fd, info.st.st_mode & 07777);  // best effort to match source mode, ignore umask

    if (::fsync(stagingFd.fd) < 0) {
        set_error(err, "fsync");
        cleanup_staging_if_untracked();
        return false;
    }

    stagingFd = Fd();

    bool destinationCreated = false;
    if (!rename_staging_into_destination(dstDir, stagingName, dstName, overwriteExisting, destinationCreated, err)) {
        cleanup_staging_if_untracked();
        return false;
    }

    if (destinationCreated && journal) {
        if (!journal->recordFile(dstPath, err)) {
            Error cleanupErr;
            LinuxFsSafety::unlink_under(dstDir, dstName, cleanupErr, true);
            return false;
        }
    }

    return true;
}

bool copy_dir_at(int srcDirFd,
                 const StatInfo& info,
                 int dstDir,
                 const char* dstName,
                 ProgressInfo& progress,
                 const ProgressCallback& cb,
                 Error& err,
                 int depth,
                 const std::string& dstPath,
                 CopyJournal* journal,
                 bool preserveOwnership,
                 bool overwriteExisting);

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
                   bool preserveOwnership,
                   bool overwriteExisting) {
    if (depth > kMaxRecursionDepth) {
        err.code = ELOOP;
        err.message = "Maximum recursion depth exceeded";
        return false;
    }

    Fd srcFd;
    StatInfo info;
    SourceEntryType type = SourceEntryType::RegularFile;
    if (!open_source_entry_for_copy(srcDir, srcName, srcFd, info, type, err)) {
        return false;
    }

    if (!should_continue(cb, progress)) {
        err.code = ECANCELED;
        err.message = "Cancelled";
        return false;
    }

    switch (type) {
        case SourceEntryType::Directory:
            return copy_dir_at(srcFd.fd, info, dstDir, dstName, progress, cb, err, depth + 1, dstPath, journal,
                               preserveOwnership, overwriteExisting);
        case SourceEntryType::RegularFile:
            return copy_file_at(srcFd.fd, dstDir, dstName, info, progress, cb, err, dstPath, journal, preserveOwnership,
                                overwriteExisting);
        case SourceEntryType::Symlink:
            return copy_symlink_at(srcDir, srcName, srcFd.fd, dstDir, dstName, info, err, dstPath, journal,
                                   preserveOwnership, overwriteExisting);
    }

    err.code = ENOTSUP;
    err.message = "Unsupported file type";
    return false;
}

bool copy_dir_at(int srcDirFd,
                 const StatInfo& info,
                 int dstDir,
                 const char* dstName,
                 ProgressInfo& progress,
                 const ProgressCallback& cb,
                 Error& err,
                 int depth,
                 const std::string& dstPath,
                 CopyJournal* journal,
                 bool preserveOwnership,
                 bool overwriteExisting) {
    if (!S_ISDIR(info.st.st_mode)) {
        err.code = ENOTDIR;
        err.message = "Not a directory";
        return false;
    }

    // Create dest dir
    if (!LinuxFsSafety::mkdir_under(dstDir, dstName, info.st.st_mode & 0777, err)) {
        if (err.code != EEXIST) {
            return false;
        }
        if (!overwriteExisting) {
            return false;
        }
        err = {};
    }
    else {
        if (journal) {
            if (!journal->recordDir(dstPath, err)) {
                return false;
            }
        }
    }

    Fd newDst;
    if (!open_dir_nofollow_at(dstDir, dstName, newDst, err)) {
        return false;
    }

    Fd srcIter;
    if (!duplicate_fd(srcDirFd, srcIter, err)) {
        return false;
    }

    Dir dir(::fdopendir(srcIter.fd));
    if (!dir.valid()) {
        set_error(err, "fdopendir");
        return false;
    }
    srcIter.fd = -1;

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
        if (!child || child[0] == '\0' || is_dot_or_dotdot(child)) {
            continue;
        }

        const std::string childDstPath = join_child_path(dstPath, child);
        if (!copy_entry_at(::dirfd(dir.dir), child, newDst.fd, child, progress, cb, err, depth + 1, childDstPath,
                           journal, preserveOwnership, overwriteExisting)) {
            return false;
        }
    }

    struct timespec times[2];
    times[0] = info.st.st_atim;
    times[1] = info.st.st_mtim;
    ::futimens(newDst.fd, times);
    if (preserveOwnership) {
        ::fchown(newDst.fd, info.st.st_uid, info.st.st_gid);
    }
    ::fchmod(newDst.fd, info.st.st_mode & 07777);

    return true;
}

bool delete_at(int dirfd, const char* name, ProgressInfo& progress, const ProgressCallback& cb, Error& err, int depth) {
    if (depth > kMaxRecursionDepth) {
        err.code = ELOOP;
        err.message = "Maximum recursion depth exceeded";
        return false;
    }

    Fd subDir;
    const bool isDirectory = open_under_fd(dirfd, name, open_dir_flags(), 0, kResolveNoSymlinks, subDir, err);
    if (!isDirectory && err.code != ENOTDIR && err.code != ELOOP) {
        return false;
    }
    if (!isDirectory) {
        err = {};
    }

    if (!should_continue(cb, progress)) {
        err.code = ECANCELED;
        err.message = "Cancelled";
        return false;
    }

    if (isDirectory) {
        Fd iterFd;
        if (!duplicate_fd(subDir.fd, iterFd, err)) {
            return false;
        }

        Dir dir(::fdopendir(iterFd.fd));
        if (!dir.valid()) {
            set_error(err, "fdopendir");
            return false;
        }
        iterFd.fd = -1;

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
            if (!child || child[0] == '\0' || is_dot_or_dotdot(child)) {
                continue;
            }
            if (!delete_at(::dirfd(dir.dir), child, progress, cb, err, depth + 1)) {
                return false;
            }
        }

        if (!LinuxFsSafety::rmdir_under(dirfd, name, err)) {
            return false;
        }
    }
    else {
        if (!LinuxFsSafety::unlink_under(dirfd, name, err)) {
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

    std::string parentPath;
    std::string leaf;
    split_parent_and_name(path, parentPath, leaf);
    if (leaf.empty() || is_dot_or_dotdot(leaf.c_str())) {
        err.code = EINVAL;
        err.message = "Invalid destination path";
        return false;
    }

    Fd parentFd;
    if (!open_dir_path_secure(parentPath, /*createMissing=*/true, parentFd, err)) {
        return false;
    }

    return LinuxFsSafety::atomic_replace_under(parentFd.fd, leaf, data, size, 0600, err);
}

bool make_dir_parents(const std::string& path, Error& err) {
    err = {};
    if (path.empty()) {
        return true;
    }

    bool absolute = false;
    std::vector<std::string> components;
    split_path_components(path, absolute, components);

    std::string relPath;
    for (const auto& comp : components) {
        if (comp.empty() || comp == ".") {
            continue;
        }
        if (!relPath.empty()) {
            relPath.push_back('/');
        }
        relPath += comp;
    }

    if (relPath.empty()) {
        return true;
    }

    Fd anchor;
    if (!open_anchor_dir(absolute, anchor, err)) {
        return false;
    }

    LinuxFsSafety::Fd opened;
    if (!LinuxFsSafety::open_dir_path_under(anchor.fd, relPath, true, 0777, opened, err)) {
        if (err.code == EEXIST) {
            err = {};
            return true;
        }
        return false;
    }

    return true;
}

bool set_permissions(const std::string& path, unsigned int mode, Error& err) {
    err = {};
    std::string parent;
    std::string leaf;
    split_parent_and_name(path, parent, leaf);
    if (leaf.empty() || is_dot_or_dotdot(leaf.c_str())) {
        err.code = EINVAL;
        err.message = "Invalid permissions path";
        return false;
    }

    Fd parentFd;
    if (!open_dir_path_secure(parent, /*createMissing=*/false, parentFd, err)) {
        return false;
    }

    if (::fchmodat(parentFd.fd, leaf.c_str(), static_cast<mode_t>(mode), 0) < 0) {
        set_error(err, "fchmodat");
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
    std::string parent;
    std::string leaf;
    split_parent_and_name(path, parent, leaf);
    if (leaf.empty() || is_dot_or_dotdot(leaf.c_str())) {
        err.code = EINVAL;
        err.message = "Invalid time path";
        return false;
    }

    Fd parentFd;
    if (!open_dir_path_secure(parent, /*createMissing=*/false, parentFd, err)) {
        return false;
    }

    struct timespec times[2];
    times[0].tv_sec = atimeSec;
    times[0].tv_nsec = atimeNsec;
    times[1].tv_sec = mtimeSec;
    times[1].tv_nsec = mtimeNsec;

    if (::utimensat(parentFd.fd, leaf.c_str(), times, 0) < 0) {
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
               bool preserveOwnership,
               bool overwriteExisting) {
    err = {};

    std::string srcParent, srcName;
    split_parent_and_name(source, srcParent, srcName);
    std::string destParent, destName;
    split_parent_and_name(destination, destParent, destName);

    if (srcName.empty() || is_dot_or_dotdot(srcName.c_str())) {
        err.code = EINVAL;
        err.message = "Invalid source path";
        return false;
    }
    if (destName.empty() || is_dot_or_dotdot(destName.c_str())) {
        err.code = EINVAL;
        err.message = "Invalid destination path";
        return false;
    }

    Fd srcParentFd;
    if (!open_dir_path_secure(srcParent, /*createMissing=*/false, srcParentFd, err)) {
        return false;
    }

    Fd destParentFd;
    if (!open_dir_path_secure(destParent, /*createMissing=*/true, destParentFd, err)) {
        return false;
    }

    CopyJournal journal;
    if (!journal.init(destParentFd.fd, err)) {
        return false;
    }

    const bool ok = copy_entry_at(srcParentFd.fd, srcName.c_str(), destParentFd.fd, destName.c_str(), progress,
                                  callback, err, 0, destName, &journal, preserveOwnership, overwriteExisting);

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
               bool preserveOwnership,
               bool overwriteExisting) {
    err = {};

    if (!forceCopyFallbackForTests) {
        std::string srcParent;
        std::string srcName;
        split_parent_and_name(source, srcParent, srcName);

        std::string destParent;
        std::string destName;
        split_parent_and_name(destination, destParent, destName);

        if (srcName.empty() || is_dot_or_dotdot(srcName.c_str())) {
            err.code = EINVAL;
            err.message = "Invalid source path";
            return false;
        }
        if (destName.empty() || is_dot_or_dotdot(destName.c_str())) {
            err.code = EINVAL;
            err.message = "Invalid destination path";
            return false;
        }

        Fd srcParentFd;
        if (!open_dir_path_secure(srcParent, /*createMissing=*/false, srcParentFd, err)) {
            return false;
        }

        Fd destParentFd;
        if (!open_dir_path_secure(destParent, /*createMissing=*/false, destParentFd, err)) {
            return false;
        }

        if (!should_continue(callback, progress)) {
            err.code = ECANCELED;
            err.message = "Cancelled";
            return false;
        }

        unsigned int renameFlags = 0;
        if (!overwriteExisting) {
#ifdef RENAME_NOREPLACE
            renameFlags = static_cast<unsigned int>(RENAME_NOREPLACE);
#else
            err.code = ENOSYS;
            err.message = "renameat2 RENAME_NOREPLACE is required but unavailable on this kernel";
            return false;
#endif
        }

        if (LinuxFsSafety::rename_under(srcParentFd.fd, srcName, destParentFd.fd, destName, renameFlags, err)) {
            progress.filesDone += 1;
            progress.currentPath = source;
            if (!should_continue(callback, progress)) {
                err.code = ECANCELED;
                err.message = "Cancelled";
                return false;
            }
            return true;
        }

        if (err.code != EXDEV) {
            return false;
        }
        err = {};
    }

    // Cross-device or forced fallback: copy then delete
    if (!copy_path(source, destination, progress, callback, err, preserveOwnership, overwriteExisting)) {
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
    if (name.empty() || is_dot_or_dotdot(name.c_str())) {
        err.code = EINVAL;
        err.message = "Invalid delete path";
        return false;
    }

    Fd parentFd;
    if (!open_dir_path_secure(parent, /*createMissing=*/false, parentFd, err)) {
        return false;
    }

    if (!delete_at(parentFd.fd, name.c_str(), progress, callback, err, 0)) {
        return false;
    }
    return true;
}

}  // namespace PCManFM::FsOps
