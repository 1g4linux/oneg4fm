/*
 * POSIX-based archive writer built on libarchive
 * src/core/archive_writer.h
 */

#ifndef PCMANFM_ARCHIVE_WRITER_H
#define PCMANFM_ARCHIVE_WRITER_H

#include "fs_ops.h"

#include <string>
#include <vector>

namespace Oneg4FM::ArchiveWriter {

// Create a tar archive (compressed with zstd if available in libarchive) at |destination|
// from the given list of native byte-string paths. Progress and cancellation use the same
// callback contract as fs_ops.
bool create_tar_zst(const std::vector<std::string>& sources,
                    const std::string& destination,
                    FsOps::ProgressInfo& progress,
                    const FsOps::ProgressCallback& callback,
                    FsOps::Error& err);

// Extract a tar or tar.zst archive at |archivePath| into |destinationDir|. The destination
// directory is created and must not already exist. Progress/cancel semantics match fs_ops.
bool extract_tar_zst(const std::string& archivePath,
                     const std::string& destinationDir,
                     FsOps::ProgressInfo& progress,
                     const FsOps::ProgressCallback& callback,
                     FsOps::Error& err);

}  // namespace Oneg4FM::ArchiveWriter

#endif  // PCMANFM_ARCHIVE_WRITER_H
