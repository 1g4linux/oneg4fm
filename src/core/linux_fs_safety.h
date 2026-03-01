/*
 * Linux filesystem safety wrappers for dirfd-anchored operations.
 * src/core/linux_fs_safety.h
 */

#ifndef PCMANFM_LINUX_FS_SAFETY_H
#define PCMANFM_LINUX_FS_SAFETY_H

#include "fs_ops.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <linux/openat2.h>
#include <sys/types.h>

struct statx;

namespace Oneg4FM::LinuxFsSafety {

class Fd {
   public:
    explicit Fd(int fd = -1) noexcept;
    ~Fd();

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept;
    void reset(int fd = -1) noexcept;

   private:
    int fd_ = -1;
};

int open_dir_flags();

bool duplicate_fd(int fd, Fd& out, FsOps::Error& err);

constexpr std::uint64_t kResolveNoSymlinks = static_cast<std::uint64_t>(RESOLVE_BENEATH) |
                                             static_cast<std::uint64_t>(RESOLVE_NO_SYMLINKS) |
                                             static_cast<std::uint64_t>(RESOLVE_NO_MAGICLINKS);
constexpr std::uint64_t kResolveNoMagiclinks =
    static_cast<std::uint64_t>(RESOLVE_BENEATH) | static_cast<std::uint64_t>(RESOLVE_NO_MAGICLINKS);

bool open_under(int rootfd,
                const std::string& relpath,
                int flags,
                mode_t mode,
                std::uint64_t resolve_flags,
                Fd& out,
                FsOps::Error& err);

bool open_dir_path_under(int rootfd,
                         const std::string& relpath,
                         bool create_missing,
                         mode_t mode,
                         Fd& out,
                         FsOps::Error& err);

bool open_dir_path_under(int rootfd,
                         const std::vector<std::string>& components,
                         bool create_missing,
                         mode_t mode,
                         Fd& out,
                         FsOps::Error& err);

bool resolve_parent_dir_under(int rootfd,
                              const std::string& relpath,
                              bool create_parents,
                              Fd& out_parent,
                              std::string& out_leaf,
                              FsOps::Error& err);

bool mkdir_under(int rootfd, const std::string& relpath, mode_t mode, FsOps::Error& err, bool allow_existing = false);

bool unlink_under(int rootfd, const std::string& relpath, FsOps::Error& err, bool allow_missing = false);
bool rmdir_under(int rootfd,
                 const std::string& relpath,
                 FsOps::Error& err,
                 bool allow_missing = false,
                 bool allow_not_empty = false);

bool rename_under(int old_rootfd,
                  const std::string& old_relpath,
                  int new_rootfd,
                  const std::string& new_relpath,
                  unsigned int flags,
                  FsOps::Error& err);

bool atomic_replace_under(int rootfd,
                          const std::string& relpath,
                          const std::uint8_t* data,
                          std::size_t size,
                          mode_t mode,
                          FsOps::Error& err);

bool statx_under(int rootfd,
                 const std::string& relpath,
                 int flags,
                 unsigned int mask,
                 struct statx& out,
                 FsOps::Error& err);

}  // namespace Oneg4FM::LinuxFsSafety

#endif  // PCMANFM_LINUX_FS_SAFETY_H
