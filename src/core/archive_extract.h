/*
 * POSIX-only archive extraction helpers built on libarchive
 * src/core/archive_extract.h
 */

#ifndef PCMANFM_ARCHIVE_EXTRACT_H
#define PCMANFM_ARCHIVE_EXTRACT_H

#include "fs_ops.h"

#include <string>

namespace Oneg4FM::ArchiveExtract {

struct Options {
    bool overwriteExisting = true;
    bool keepPermissions = true;
    bool keepOwnership = false;
    bool keepXattrs = true;
    bool keepSymlinks = true;
    bool enableFilterThreads = true;
    unsigned maxFilterThreads = 0;  // 0 = use hardware_concurrency or libarchive default
};

// Extracts a wide range of archive formats (zip, tar/tgz/tbz2/txz/tzst/tlz4, cpio, ar, 7z, iso,
// xar, rpm, deb, etc.) into |destinationDir|. The destination directory must not already exist.
// Progress/cancel semantics match FsOps: the callback can return false to request cancellation.
bool extract_archive(const std::string& archivePath,
                     const std::string& destinationDir,
                     FsOps::ProgressInfo& progress,
                     const FsOps::ProgressCallback& callback,
                     FsOps::Error& err,
                     const Options& opts = {});

}  // namespace Oneg4FM::ArchiveExtract

#endif  // PCMANFM_ARCHIVE_EXTRACT_H
