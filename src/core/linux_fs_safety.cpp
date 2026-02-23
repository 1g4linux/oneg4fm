/*
 * Linux filesystem safety wrappers for dirfd-anchored operations.
 * src/core/linux_fs_safety.cpp
 */

#include "linux_fs_safety.h"

#include <cerrno>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SYS_openat2
#ifdef __NR_openat2
#define SYS_openat2 __NR_openat2
#endif
#endif

#ifndef SYS_renameat2
#ifdef __NR_renameat2
#define SYS_renameat2 __NR_renameat2
#endif
#endif

#ifndef SYS_statx
#ifdef __NR_statx
#define SYS_statx __NR_statx
#endif
#endif

namespace PCManFM::LinuxFsSafety {
namespace {

inline void set_error(FsOps::Error& err, const char* context) {
    err.code = errno;
    err.message = std::string(context) + ": " + std::strerror(errno);
}

inline void set_required_kernel_error(FsOps::Error& err, const char* syscall_name) {
    err.code = ENOSYS;
    err.message = std::string(syscall_name) + " is required but unavailable on this kernel";
}

inline void set_openat2_error(FsOps::Error& err, const char* context) {
    if (errno == ENOSYS) {
        set_required_kernel_error(err, "openat2");
        return;
    }
    set_error(err, context);
}

inline void set_renameat2_error(FsOps::Error& err, const char* context) {
    if (errno == ENOSYS) {
        set_required_kernel_error(err, "renameat2");
        return;
    }
    set_error(err, context);
}

inline void set_statx_error(FsOps::Error& err, const char* context) {
    if (errno == ENOSYS) {
        set_required_kernel_error(err, "statx");
        return;
    }
    set_error(err, context);
}

int openat2_raw(int dirfd, const char* path, int flags, mode_t mode, std::uint64_t resolve) {
#ifdef SYS_openat2
    struct open_how how{};
    how.flags = static_cast<std::uint64_t>(flags);
    how.mode = static_cast<std::uint64_t>(mode);
    how.resolve = resolve;
    return static_cast<int>(::syscall(SYS_openat2, dirfd, path, &how, sizeof(how)));
#else
    (void)dirfd;
    (void)path;
    (void)flags;
    (void)mode;
    (void)resolve;
    errno = ENOSYS;
    return -1;
#endif
}

int renameat2_raw(int old_dirfd, const char* old_path, int new_dirfd, const char* new_path, unsigned int flags) {
#ifdef SYS_renameat2
    return static_cast<int>(::syscall(SYS_renameat2, old_dirfd, old_path, new_dirfd, new_path, flags));
#else
    (void)old_dirfd;
    (void)old_path;
    (void)new_dirfd;
    (void)new_path;
    (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

int statx_raw(int dirfd, const char* path, int flags, unsigned int mask, struct statx* stx) {
#ifdef SYS_statx
    return static_cast<int>(::syscall(SYS_statx, dirfd, path, flags, mask, stx));
#else
    (void)dirfd;
    (void)path;
    (void)flags;
    (void)mask;
    (void)stx;
    errno = ENOSYS;
    return -1;
#endif
}

bool parse_relative_components(const std::string& relpath,
                               bool allow_empty,
                               std::vector<std::string>& out_components,
                               FsOps::Error& err) {
    out_components.clear();
    if (relpath.empty() || relpath == ".") {
        if (!allow_empty) {
            err.code = EINVAL;
            err.message = "Path is empty";
            return false;
        }
        return true;
    }

    if (!relpath.empty() && relpath.front() == '/') {
        err.code = EINVAL;
        err.message = "Path must be relative to root fd";
        return false;
    }

    std::string current;
    for (char c : relpath) {
        if (c == '/') {
            if (current.empty()) {
                continue;
            }
            if (current == ".") {
                current.clear();
                continue;
            }
            if (current == "..") {
                err.code = EINVAL;
                err.message = "Path traversal is not allowed";
                return false;
            }
            out_components.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    if (!current.empty()) {
        if (current == ".") {
            current.clear();
        }
        else if (current == "..") {
            err.code = EINVAL;
            err.message = "Path traversal is not allowed";
            return false;
        }
        else {
            out_components.push_back(std::move(current));
        }
    }

    if (!allow_empty && out_components.empty()) {
        err.code = EINVAL;
        err.message = "Path is empty";
        return false;
    }
    return true;
}

std::string join_components(const std::vector<std::string>& components) {
    std::string joined;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i != 0) {
            joined.push_back('/');
        }
        joined += components[i];
    }
    return joined;
}

bool renameat2_name(int old_dirfd,
                    const char* old_name,
                    int new_dirfd,
                    const char* new_name,
                    unsigned int flags,
                    FsOps::Error& err) {
    if (renameat2_raw(old_dirfd, old_name, new_dirfd, new_name, flags) != 0) {
        set_renameat2_error(err, "renameat2");
        return false;
    }
    return true;
}

bool write_all_fd(int fd, const std::uint8_t* data, std::size_t size, FsOps::Error& err) {
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

std::string build_temp_name(const char* prefix, unsigned int attempt) {
    const unsigned int pid = static_cast<unsigned int>(::getpid());
    const unsigned int t = static_cast<unsigned int>(::time(nullptr));
    return std::string(prefix) + std::to_string(pid) + "." + std::to_string(t) + "." + std::to_string(attempt);
}

bool is_tmpfile_not_supported(int errnum) {
    return errnum == EOPNOTSUPP || errnum == ENOTSUP || errnum == EINVAL || errnum == EISDIR || errnum == ENOENT;
}

bool create_temp_file_named_under(int parent_fd, mode_t mode, Fd& out_fd, std::string& out_name, FsOps::Error& err) {
    int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    for (unsigned int attempt = 0; attempt < 256; ++attempt) {
        const std::string candidate = build_temp_name(".pcmanfm.tmp.", attempt);
        Fd fd;
        if (open_under(parent_fd, candidate, flags, mode, kResolveNoSymlinks, fd, err)) {
            out_name = candidate;
            out_fd = std::move(fd);
            return true;
        }

        if (err.code == EEXIST) {
            err = {};
            continue;
        }
        return false;
    }

    err.code = EEXIST;
    err.message = "Failed to allocate unique temporary file name";
    return false;
}

bool link_tmpfile_under(int tmp_fd, int parent_fd, std::string& out_name, FsOps::Error& err) {
    for (unsigned int attempt = 0; attempt < 256; ++attempt) {
        const std::string candidate = build_temp_name(".pcmanfm.tmp.link.", attempt);
        if (::linkat(tmp_fd, "", parent_fd, candidate.c_str(), AT_EMPTY_PATH) == 0) {
            out_name = candidate;
            return true;
        }
        if (errno == EEXIST) {
            continue;
        }
        set_error(err, "linkat");
        return false;
    }

    err.code = EEXIST;
    err.message = "Failed to allocate unique temporary link name";
    return false;
}

void best_effort_unlink_name(int parent_fd, const std::string& name) {
    if (name.empty()) {
        return;
    }
    ::unlinkat(parent_fd, name.c_str(), 0);
}

bool open_tmpfile_under(int parent_fd, mode_t mode, Fd& out_fd, FsOps::Error& err) {
    int flags = O_RDWR | O_TMPFILE | O_CLOEXEC;
    Fd fd(openat2_raw(parent_fd, ".", flags, mode, kResolveNoSymlinks));
    if (!fd.valid()) {
        if (errno == ENOSYS) {
            set_openat2_error(err, "openat2");
            return false;
        }
        set_error(err, "openat2");
        return false;
    }
    out_fd = std::move(fd);
    return true;
}

}  // namespace

Fd::Fd(int fd) noexcept : fd_(fd) {}

Fd::~Fd() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

Fd::Fd(Fd&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int Fd::release() noexcept {
    const int out = fd_;
    fd_ = -1;
    return out;
}

void Fd::reset(int fd) noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}

int open_dir_flags() {
    int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

bool duplicate_fd(int fd, Fd& out, FsOps::Error& err) {
    Fd dup_fd(::dup(fd));
    if (!dup_fd.valid()) {
        set_error(err, "dup");
        return false;
    }
    out = std::move(dup_fd);
    return true;
}

bool open_under(int rootfd,
                const std::string& relpath,
                int flags,
                mode_t mode,
                std::uint64_t resolve_flags,
                Fd& out,
                FsOps::Error& err) {
    std::vector<std::string> components;
    if (!parse_relative_components(relpath, true, components, err)) {
        return false;
    }

    const std::string normalized = components.empty() ? std::string(".") : join_components(components);
    const std::uint64_t resolve =
        resolve_flags | static_cast<std::uint64_t>(RESOLVE_BENEATH) | static_cast<std::uint64_t>(RESOLVE_NO_MAGICLINKS);

    Fd fd(openat2_raw(rootfd, normalized.c_str(), flags, mode, resolve));
    if (!fd.valid()) {
        set_openat2_error(err, "openat2");
        return false;
    }
    out = std::move(fd);
    return true;
}

bool open_dir_path_under(int rootfd,
                         const std::string& relpath,
                         bool create_missing,
                         mode_t mode,
                         Fd& out,
                         FsOps::Error& err) {
    std::vector<std::string> components;
    if (!parse_relative_components(relpath, true, components, err)) {
        return false;
    }
    return open_dir_path_under(rootfd, components, create_missing, mode, out, err);
}

bool open_dir_path_under(int rootfd,
                         const std::vector<std::string>& components,
                         bool create_missing,
                         mode_t mode,
                         Fd& out,
                         FsOps::Error& err) {
    if (!duplicate_fd(rootfd, out, err)) {
        return false;
    }

    for (const auto& comp : components) {
        if (comp.empty() || comp == ".") {
            continue;
        }
        if (comp == ".." || comp.find('/') != std::string::npos) {
            err.code = EINVAL;
            err.message = "Path traversal is not allowed";
            return false;
        }

        if (create_missing && ::mkdirat(out.get(), comp.c_str(), mode) != 0 && errno != EEXIST) {
            set_error(err, "mkdirat");
            return false;
        }

        Fd next;
        if (!open_under(out.get(), comp, open_dir_flags(), 0, kResolveNoSymlinks, next, err)) {
            return false;
        }
        out = std::move(next);
    }
    return true;
}

bool resolve_parent_dir_under(int rootfd,
                              const std::string& relpath,
                              bool create_parents,
                              Fd& out_parent,
                              std::string& out_leaf,
                              FsOps::Error& err) {
    std::vector<std::string> components;
    if (!parse_relative_components(relpath, false, components, err)) {
        return false;
    }
    if (components.empty()) {
        err.code = EINVAL;
        err.message = "Path is empty";
        return false;
    }

    const std::vector<std::string> parent_components(components.begin(), components.end() - 1);
    if (!open_dir_path_under(rootfd, parent_components, create_parents, 0777, out_parent, err)) {
        return false;
    }

    out_leaf = components.back();
    return true;
}

bool mkdir_under(int rootfd, const std::string& relpath, mode_t mode, FsOps::Error& err, bool allow_existing) {
    Fd parent;
    std::string leaf;
    if (!resolve_parent_dir_under(rootfd, relpath, false, parent, leaf, err)) {
        return false;
    }

    if (::mkdirat(parent.get(), leaf.c_str(), mode) == 0) {
        return true;
    }
    if (allow_existing && errno == EEXIST) {
        return true;
    }

    set_error(err, "mkdirat");
    return false;
}

bool unlink_under(int rootfd, const std::string& relpath, FsOps::Error& err, bool allow_missing) {
    Fd parent;
    std::string leaf;
    if (!resolve_parent_dir_under(rootfd, relpath, false, parent, leaf, err)) {
        return false;
    }

    if (::unlinkat(parent.get(), leaf.c_str(), 0) == 0) {
        return true;
    }
    if (allow_missing && errno == ENOENT) {
        return true;
    }

    set_error(err, "unlinkat");
    return false;
}

bool rmdir_under(int rootfd, const std::string& relpath, FsOps::Error& err, bool allow_missing, bool allow_not_empty) {
    Fd parent;
    std::string leaf;
    if (!resolve_parent_dir_under(rootfd, relpath, false, parent, leaf, err)) {
        return false;
    }

    if (::unlinkat(parent.get(), leaf.c_str(), AT_REMOVEDIR) == 0) {
        return true;
    }
    if (allow_missing && errno == ENOENT) {
        return true;
    }
    if (allow_not_empty && errno == ENOTEMPTY) {
        return true;
    }

    set_error(err, "unlinkat");
    return false;
}

bool rename_under(int old_rootfd,
                  const std::string& old_relpath,
                  int new_rootfd,
                  const std::string& new_relpath,
                  unsigned int flags,
                  FsOps::Error& err) {
    Fd old_parent;
    std::string old_leaf;
    if (!resolve_parent_dir_under(old_rootfd, old_relpath, false, old_parent, old_leaf, err)) {
        return false;
    }

    Fd new_parent;
    std::string new_leaf;
    if (!resolve_parent_dir_under(new_rootfd, new_relpath, false, new_parent, new_leaf, err)) {
        return false;
    }

    return renameat2_name(old_parent.get(), old_leaf.c_str(), new_parent.get(), new_leaf.c_str(), flags, err);
}

bool atomic_replace_under(int rootfd,
                          const std::string& relpath,
                          const std::uint8_t* data,
                          std::size_t size,
                          mode_t mode,
                          FsOps::Error& err) {
    if (size > 0 && data == nullptr) {
        err.code = EINVAL;
        err.message = "Null data pointer with non-zero size";
        return false;
    }

    Fd parent;
    std::string leaf;
    if (!resolve_parent_dir_under(rootfd, relpath, false, parent, leaf, err)) {
        return false;
    }

    std::string temp_name;
    Fd tmp_fd;
    bool need_fallback_temp_file = true;

    FsOps::Error tmpfile_err;
    if (open_tmpfile_under(parent.get(), mode, tmp_fd, tmpfile_err)) {
        if (!write_all_fd(tmp_fd.get(), data, size, err)) {
            return false;
        }
        if (::fsync(tmp_fd.get()) != 0) {
            set_error(err, "fsync");
            return false;
        }

        FsOps::Error link_err;
        if (link_tmpfile_under(tmp_fd.get(), parent.get(), temp_name, link_err)) {
            need_fallback_temp_file = false;
        }
        else if (!is_tmpfile_not_supported(link_err.code) && link_err.code != EPERM) {
            err = link_err;
            return false;
        }
    }
    else {
        if (tmpfile_err.code == ENOSYS) {
            err = tmpfile_err;
            return false;
        }
        if (!is_tmpfile_not_supported(tmpfile_err.code) && tmpfile_err.code != EPERM) {
            err = tmpfile_err;
            return false;
        }
    }

    if (need_fallback_temp_file) {
        temp_name.clear();
        tmp_fd.reset();
        if (!create_temp_file_named_under(parent.get(), mode, tmp_fd, temp_name, err)) {
            return false;
        }
        if (!write_all_fd(tmp_fd.get(), data, size, err)) {
            best_effort_unlink_name(parent.get(), temp_name);
            return false;
        }
        if (::fsync(tmp_fd.get()) != 0) {
            set_error(err, "fsync");
            best_effort_unlink_name(parent.get(), temp_name);
            return false;
        }
    }

    tmp_fd.reset();
    if (!renameat2_name(parent.get(), temp_name.c_str(), parent.get(), leaf.c_str(), 0, err)) {
        best_effort_unlink_name(parent.get(), temp_name);
        return false;
    }

    return true;
}

bool statx_under(int rootfd,
                 const std::string& relpath,
                 int flags,
                 unsigned int mask,
                 struct statx& out,
                 FsOps::Error& err) {
    std::vector<std::string> components;
    if (!parse_relative_components(relpath, true, components, err)) {
        return false;
    }

    const std::string normalized = components.empty() ? std::string(".") : join_components(components);
    if (statx_raw(rootfd, normalized.c_str(), flags, mask, &out) != 0) {
        set_statx_error(err, "statx");
        return false;
    }
    return true;
}

}  // namespace PCManFM::LinuxFsSafety
