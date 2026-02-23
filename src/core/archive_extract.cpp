/*
 * POSIX-only archive extraction helpers built on libarchive
 * src/core/archive_extract.cpp
 */

#include "archive_extract.h"

#include "fs_ops.h"

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace Oneg4FM::ArchiveExtract {
namespace {

using Oneg4FM::FsOps::Error;
using Oneg4FM::FsOps::ProgressCallback;
using Oneg4FM::FsOps::ProgressInfo;

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

inline void set_error(Error& err, const std::string& context) {
    err.code = errno;
    err.message = context + ": " + std::strerror(errno);
}

inline void set_archive_error(Error& err, struct archive* ar, const char* context) {
    err.code = EIO;
    const char* details = archive_error_string(ar);
    err.message = std::string(context) + ": " + (details ? details : "libarchive error");
}

bool should_continue(const ProgressCallback& cb, const ProgressInfo& info) {
    if (!cb) {
        return true;
    }
    return cb(info);
}

std::string sanitize_path(const char* raw) {
    if (!raw || raw[0] == '\0') {
        return {};
    }
    std::string_view v(raw);
    if (!v.empty() && v.front() == '/') {
        return {};
    }

    std::string result;
    std::vector<std::string> components;
    std::string current;

    for (char c : v) {
        if (c == '/') {
            if (!current.empty()) {
                if (current == ".") {
                    // skip
                }
                else if (current == "..") {
                    return {};
                }
                else {
                    components.push_back(std::move(current));
                }
                current.clear();
            }
        }
        else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        if (current == ".") {
            // skip
        }
        else if (current == "..") {
            return {};
        }
        else {
            components.push_back(std::move(current));
        }
    }

    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i > 0) {
            result.push_back('/');
        }
        result.append(components[i]);
    }
    return result;
}

struct RelativePath {
    std::string normalized;
    std::vector<std::string> components;
};

bool parse_relative_path(const char* raw, RelativePath& out) {
    out = {};
    out.normalized = sanitize_path(raw);
    if (out.normalized.empty()) {
        return false;
    }

    std::string current;
    for (char c : out.normalized) {
        if (c == '/') {
            if (current.empty()) {
                return false;
            }
            out.components.emplace_back(std::move(current));
            current.clear();
        }
        else {
            current.push_back(c);
        }
    }
    if (current.empty()) {
        return false;
    }
    out.components.emplace_back(std::move(current));
    return !out.components.empty();
}

std::string parent_dir(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

bool ensure_destination_root(const std::string& destinationDir, Error& err) {
    struct stat st{};
    if (::lstat(destinationDir.c_str(), &st) == 0) {
        err.code = EEXIST;
        err.message = "Destination already exists";
        return false;
    }

    if (!FsOps::make_dir_parents(parent_dir(destinationDir), err)) {
        return false;
    }

    if (::mkdir(destinationDir.c_str(), 0777) != 0) {
        set_error(err, "mkdir");
        return false;
    }
    return true;
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

int open_dir_flags() {
    int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

bool open_dir_nofollow_at(int parentFd, const std::string& name, Fd& out, Error& err) {
    Fd next(::openat(parentFd, name.c_str(), open_dir_flags()));
    if (!next.valid()) {
        set_error(err, "openat");
        return false;
    }
    out = std::move(next);
    return true;
}

bool ensure_dir_components(int rootFd,
                           const std::vector<std::string>& components,
                           bool createMissing,
                           mode_t mode,
                           Fd& outDir,
                           Error& err) {
    if (!duplicate_fd(rootFd, outDir, err)) {
        return false;
    }

    for (const auto& comp : components) {
        if (createMissing) {
            if (::mkdirat(outDir.fd, comp.c_str(), mode) != 0 && errno != EEXIST) {
                set_error(err, "mkdirat");
                return false;
            }
        }
        Fd next;
        if (!open_dir_nofollow_at(outDir.fd, comp, next, err)) {
            return false;
        }
        outDir = std::move(next);
    }
    return true;
}

bool resolve_parent_dir(int rootFd,
                        const RelativePath& relPath,
                        bool createParents,
                        Fd& outParent,
                        std::string& outLeaf,
                        Error& err) {
    if (relPath.components.empty()) {
        err.code = EINVAL;
        err.message = "Empty path components";
        return false;
    }

    const std::vector<std::string> parentComponents(relPath.components.begin(), relPath.components.end() - 1);
    if (!ensure_dir_components(rootFd, parentComponents, createParents, 0777, outParent, err)) {
        return false;
    }

    outLeaf = relPath.components.back();
    return true;
}

bool apply_xattrs(int fd, archive_entry* entry, const Options& opts, Error& err) {
    if (!opts.keepXattrs) {
        return true;
    }
    if (fd < 0) {
        err.code = EINVAL;
        err.message = "Invalid file descriptor for xattrs";
        return false;
    }

    const char* name = nullptr;
    const void* value = nullptr;
    std::size_t size = 0;
    archive_entry_xattr_reset(entry);
    while (archive_entry_xattr_next(entry, &name, &value, &size) == ARCHIVE_OK) {
        if (!name || !value) {
            continue;
        }
        int rc = ::fsetxattr(fd, name, value, size, 0);
        if (rc != 0 && errno != ENOTSUP && errno != EPERM) {
            set_error(err, "setxattr");
            return false;
        }
    }
    return true;
}

void populate_times(archive_entry* entry, struct timespec times[2]) {
    times[0].tv_sec = archive_entry_atime(entry);
    times[0].tv_nsec = archive_entry_atime_nsec(entry);
    times[1].tv_sec = archive_entry_mtime(entry);
    times[1].tv_nsec = archive_entry_mtime_nsec(entry);
}

void apply_metadata_fd(int fd, archive_entry* entry, const Options& opts, bool isSymlink) {
    if (fd < 0) {
        return;
    }

    if (opts.keepPermissions && !isSymlink) {
        const mode_t m = archive_entry_perm(entry);
        if (m != 0) {
            ::fchmod(fd, m);
        }
    }

    if (opts.keepOwnership) {
        const uid_t uid = archive_entry_uid(entry);
        const gid_t gid = archive_entry_gid(entry);
        ::fchown(fd, uid, gid);
    }

    struct timespec times[2];
    populate_times(entry, times);
    ::futimens(fd, times);
}

void apply_symlink_metadata_at(int parentFd, const std::string& name, archive_entry* entry, const Options& opts) {
    if (opts.keepOwnership) {
        const uid_t uid = archive_entry_uid(entry);
        const gid_t gid = archive_entry_gid(entry);
        ::fchownat(parentFd, name.c_str(), uid, gid, AT_SYMLINK_NOFOLLOW);
    }

    struct timespec times[2];
    populate_times(entry, times);
    ::utimensat(parentFd, name.c_str(), times, AT_SYMLINK_NOFOLLOW);
}

unsigned thread_count_from_opts(const Options& opts) {
    if (!opts.enableFilterThreads) {
        return 1;
    }
    if (opts.maxFilterThreads > 0) {
        return opts.maxFilterThreads;
    }
    const unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? hc : 1;
}

void configure_filter_threads(struct archive* ar, const Options& opts) {
    const unsigned threads = thread_count_from_opts(opts);
    if (threads <= 1) {
        return;
    }

    const std::string value = std::to_string(threads);
    constexpr const char* filters[] = {"zstd", "xz", "gzip", "bzip2", "lz4"};
    for (const char* f : filters) {
        archive_read_set_filter_option(ar, f, "threads", value.c_str());
    }
}

bool open_reader(const std::string& archivePath, const Options& opts, struct archive*& out, Error& err) {
    struct archive* ar = archive_read_new();
    if (!ar) {
        err.code = ENOMEM;
        err.message = "Failed to allocate archive reader";
        return false;
    }
    archive_read_support_filter_all(ar);
    archive_read_support_format_all(ar);
    configure_filter_threads(ar, opts);
    if (archive_read_open_filename(ar, archivePath.c_str(), 128 * 1024) != ARCHIVE_OK) {
        set_archive_error(err, ar, "archive_read_open_filename");
        archive_read_free(ar);
        return false;
    }
    out = ar;
    return true;
}

bool skip_archive_entry_data(struct archive* ar, Error& err) {
    if (archive_read_data_skip(ar) != ARCHIVE_OK) {
        set_archive_error(err, ar, "archive_read_data_skip");
        return false;
    }
    return true;
}

bool scan_archive(const std::string& archivePath, const Options& opts, ProgressInfo& progress, Error& err) {
    struct archive* ar = nullptr;
    if (!open_reader(archivePath, opts, ar, err)) {
        return false;
    }

    archive_entry* entry = nullptr;
    int r = ARCHIVE_OK;
    while ((r = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        const std::string rel = sanitize_path(rawPath);
        if (rel.empty()) {
            err.code = EINVAL;
            err.message = "Unsafe path in archive entry";
            archive_read_close(ar);
            archive_read_free(ar);
            return false;
        }
        const auto type = archive_entry_filetype(entry);
        if (type == AE_IFREG) {
            const la_int64_t sz = archive_entry_size(entry);
            if (sz > 0) {
                const std::uint64_t s = static_cast<std::uint64_t>(sz);
                if (s <= std::numeric_limits<std::uint64_t>::max() - progress.bytesTotal) {
                    progress.bytesTotal += s;
                }
            }
        }
        progress.filesTotal += 1;
        if (!skip_archive_entry_data(ar, err)) {
            archive_read_close(ar);
            archive_read_free(ar);
            return false;
        }
    }

    if (r != ARCHIVE_EOF) {
        set_archive_error(err, ar, "archive_read_next_header");
        archive_read_close(ar);
        archive_read_free(ar);
        return false;
    }

    if (archive_read_close(ar) != ARCHIVE_OK) {
        set_archive_error(err, ar, "archive_read_close");
        archive_read_free(ar);
        return false;
    }
    archive_read_free(ar);
    return true;
}

bool write_all(int fd, const void* data, std::size_t size, off_t offset, Error& err) {
    const std::uint8_t* ptr = static_cast<const std::uint8_t*>(data);
    std::size_t remaining = size;
    off_t off = offset;
    while (remaining > 0) {
        const ssize_t n = ::pwrite(fd, ptr, remaining, off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "write");
            return false;
        }
        remaining -= static_cast<std::size_t>(n);
        ptr += n;
        off += n;
    }
    return true;
}

bool extract_regular_file(struct archive* ar,
                          archive_entry* entry,
                          int rootFd,
                          const RelativePath& relPath,
                          const Options& opts,
                          ProgressInfo& progress,
                          const ProgressCallback& cb,
                          Error& err) {
    Fd parentFd;
    std::string leaf;
    if (!resolve_parent_dir(rootFd, relPath, true, parentFd, leaf, err)) {
        return false;
    }

    int flags = O_WRONLY | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    if (opts.overwriteExisting) {
        flags |= O_TRUNC;
    }
    else {
        flags |= O_EXCL;
    }

    mode_t mode = archive_entry_perm(entry);
    if (mode == 0) {
        mode = 0666;
    }

    Fd fd(::openat(parentFd.fd, leaf.c_str(), flags, mode));
    if (!fd.valid()) {
        set_error(err, "openat");
        return false;
    }

    const void* buff = nullptr;
    std::size_t size = 0;
    la_int64_t offset = 0;
    while (true) {
        const la_int64_t r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r != ARCHIVE_OK) {
            set_archive_error(err, ar, "archive_read_data_block");
            return false;
        }

        if (size > 0 && buff) {
            if (!write_all(fd.fd, buff, size, static_cast<off_t>(offset), err)) {
                return false;
            }
            progress.bytesDone += static_cast<std::uint64_t>(size);
            progress.currentPath = relPath.normalized;
            if (!should_continue(cb, progress)) {
                err.code = ECANCELED;
                err.message = "Cancelled";
                return false;
            }
        }
    }

    Error xerr;
    if (!apply_xattrs(fd.fd, entry, opts, xerr)) {
        err = xerr;
        return false;
    }
    apply_metadata_fd(fd.fd, entry, opts, false);
    progress.filesDone += 1;
    return true;
}

bool extract_directory(archive_entry* entry,
                       int rootFd,
                       const RelativePath& relPath,
                       const Options& opts,
                       ProgressInfo& progress,
                       Error& err) {
    mode_t mode = archive_entry_perm(entry);
    if (mode == 0) {
        mode = 0777;
    }

    Fd parentFd;
    std::string leaf;
    if (!resolve_parent_dir(rootFd, relPath, true, parentFd, leaf, err)) {
        return false;
    }

    if (::mkdirat(parentFd.fd, leaf.c_str(), mode) != 0 && errno != EEXIST) {
        set_error(err, "mkdirat");
        return false;
    }

    Fd dirFd;
    if (!open_dir_nofollow_at(parentFd.fd, leaf, dirFd, err)) {
        return false;
    }

    apply_metadata_fd(dirFd.fd, entry, opts, false);
    progress.filesDone += 1;
    return true;
}

bool extract_symlink(archive_entry* entry,
                     int rootFd,
                     const RelativePath& relPath,
                     const Options& opts,
                     ProgressInfo& progress,
                     Error& err) {
    if (!opts.keepSymlinks) {
        return true;
    }

    const char* target = archive_entry_symlink(entry);
    if (!target) {
        return true;
    }

    Fd parentFd;
    std::string leaf;
    if (!resolve_parent_dir(rootFd, relPath, true, parentFd, leaf, err)) {
        return false;
    }

    if (opts.overwriteExisting) {
        if (::unlinkat(parentFd.fd, leaf.c_str(), 0) != 0 && errno != ENOENT) {
            set_error(err, "unlinkat");
            return false;
        }
    }

    if (::symlinkat(target, parentFd.fd, leaf.c_str()) != 0) {
        set_error(err, "symlinkat");
        return false;
    }

    apply_symlink_metadata_at(parentFd.fd, leaf, entry, opts);
    progress.filesDone += 1;
    return true;
}

bool extract_hardlink(archive_entry* entry,
                      int rootFd,
                      const RelativePath& relPath,
                      bool overwriteExisting,
                      const std::unordered_set<std::string>& extractedEntries,
                      ProgressInfo& progress,
                      Error& err) {
    const char* target = archive_entry_hardlink(entry);
    if (!target) {
        return true;
    }

    RelativePath targetPath;
    if (!parse_relative_path(target, targetPath)) {
        err.code = EINVAL;
        err.message = "Unsafe hardlink target in archive entry";
        return false;
    }
    if (extractedEntries.find(targetPath.normalized) == extractedEntries.end()) {
        err.code = EINVAL;
        err.message = "Hardlink target must reference an already extracted archive entry";
        return false;
    }

    Fd targetParentFd;
    std::string targetLeaf;
    if (!resolve_parent_dir(rootFd, targetPath, false, targetParentFd, targetLeaf, err)) {
        return false;
    }

    Fd linkParentFd;
    std::string linkLeaf;
    if (!resolve_parent_dir(rootFd, relPath, true, linkParentFd, linkLeaf, err)) {
        return false;
    }

    if (overwriteExisting) {
        if (::unlinkat(linkParentFd.fd, linkLeaf.c_str(), 0) != 0 && errno != ENOENT) {
            set_error(err, "unlinkat");
            return false;
        }
    }

    if (::linkat(targetParentFd.fd, targetLeaf.c_str(), linkParentFd.fd, linkLeaf.c_str(), 0) != 0) {
        set_error(err, "linkat");
        return false;
    }
    progress.filesDone += 1;
    return true;
}

}  // namespace

bool extract_archive(const std::string& archivePath,
                     const std::string& destinationDir,
                     ProgressInfo& progress,
                     const ProgressCallback& callback,
                     Error& err,
                     const Options& opts) {
    progress = {};
    err = {};

    if (archivePath.empty() || destinationDir.empty()) {
        err.code = EINVAL;
        err.message = "Invalid archive or destination path";
        return false;
    }

    if (!ensure_destination_root(destinationDir, err)) {
        return false;
    }

    ProgressInfo scanProgress;
    if (!scan_archive(archivePath, opts, scanProgress, err)) {
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
        return false;
    }
    progress.bytesTotal = scanProgress.bytesTotal;
    progress.filesTotal = scanProgress.filesTotal;

    Fd rootFd(::open(destinationDir.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!rootFd.valid()) {
        set_error(err, "open");
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
        return false;
    }

    struct archive* ar = nullptr;
    if (!open_reader(archivePath, opts, ar, err)) {
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
        return false;
    }

    archive_entry* entry = nullptr;
    int readStatus = ARCHIVE_OK;
    bool ok = true;
    std::unordered_set<std::string> extractedEntries;
    while ((readStatus = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        RelativePath relPath;
        if (!parse_relative_path(rawPath, relPath)) {
            err.code = EINVAL;
            err.message = "Unsafe path in archive entry";
            ok = false;
            break;
        }

        progress.currentPath = relPath.normalized;
        if (!should_continue(callback, progress)) {
            err.code = ECANCELED;
            err.message = "Cancelled";
            ok = false;
            break;
        }

        const char* hardlink = archive_entry_hardlink(entry);
        if (hardlink) {
            if (!extract_hardlink(entry, rootFd.fd, relPath, opts.overwriteExisting, extractedEntries, progress, err)) {
                ok = false;
                break;
            }
            extractedEntries.insert(relPath.normalized);
            if (!skip_archive_entry_data(ar, err)) {
                ok = false;
                break;
            }
            continue;
        }

        const auto type = archive_entry_filetype(entry);
        switch (type) {
            case AE_IFREG: {
                if (!extract_regular_file(ar, entry, rootFd.fd, relPath, opts, progress, callback, err)) {
                    ok = false;
                }
                else {
                    extractedEntries.insert(relPath.normalized);
                }
                break;
            }
            case AE_IFDIR: {
                if (!extract_directory(entry, rootFd.fd, relPath, opts, progress, err)) {
                    ok = false;
                }
                else {
                    extractedEntries.insert(relPath.normalized);
                }
                if (ok && !skip_archive_entry_data(ar, err)) {
                    ok = false;
                }
                break;
            }
            case AE_IFLNK: {
                if (!extract_symlink(entry, rootFd.fd, relPath, opts, progress, err)) {
                    ok = false;
                }
                else if (opts.keepSymlinks && archive_entry_symlink(entry)) {
                    extractedEntries.insert(relPath.normalized);
                }
                if (ok && !skip_archive_entry_data(ar, err)) {
                    ok = false;
                }
                break;
            }
            default: {
                if (!skip_archive_entry_data(ar, err)) {  // unsupported special files or metadata entries
                    ok = false;
                }
                break;
            }
        }

        if (!ok) {
            break;
        }
    }

    if (ok) {
        if (readStatus != ARCHIVE_EOF) {
            set_archive_error(err, ar, "archive_read_next_header");
            ok = false;
        }
    }
    const int closeStatus = archive_read_close(ar);
    if (ok && closeStatus != ARCHIVE_OK) {
        set_archive_error(err, ar, "archive_read_close");
        ok = false;
    }
    archive_read_free(ar);

    if (!ok) {
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
    }

    return ok;
}

}  // namespace Oneg4FM::ArchiveExtract
