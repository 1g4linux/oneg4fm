#include "untrashjob.h"

#ifndef LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#define LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 0
#endif

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#include "fileinfo_p.h"
#include "../../../src/core/file_ops_contract.h"
#endif

#include <cerrno>
#include <cstdint>
#include <string>

namespace Fm {

namespace {

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
namespace CoreFileOps = Oneg4FM::FileOpsContract;

bool toNativePath(const FilePath& path, std::string& out) {
    const auto localPath = path.localPath();
    if (!localPath || localPath.get()[0] == '\0') {
        out.clear();
        return false;
    }

    out.assign(localPath.get());
    return true;
}

bool toUriPath(const FilePath& path, std::string& out) {
    const auto uri = path.uri();
    if (!uri || uri.get()[0] == '\0') {
        out.clear();
        return false;
    }

    out.assign(uri.get());
    return true;
}

bool toCoreEndpointPath(const FilePath& path, std::string& out, CoreFileOps::EndpointKind& endpointKind) {
    if (path.isNative()) {
        endpointKind = CoreFileOps::EndpointKind::NativePath;
        return toNativePath(path, out);
    }

    endpointKind = CoreFileOps::EndpointKind::Uri;
    return toUriPath(path, out);
}

FilePath toFilePathFromCorePath(const std::string& path, const FilePath& fallback) {
    if (path.empty()) {
        return fallback;
    }

    if (path.find("://") != std::string::npos) {
        return FilePath::fromUri(path.c_str());
    }

    return FilePath::fromLocalPath(path.c_str());
}

GFileInfoPtr queryInfoOrFallback(const FilePath& path, GCancellable* cancellable) {
    if (path) {
        GErrorPtr err;
        GFileInfo* info = g_file_query_info(path.gfile().get(), defaultGFileInfoQueryAttribs,
                                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &err);
        if (info) {
            return GFileInfoPtr{info, false};
        }
    }

    GFileInfo* fallback = g_file_info_new();
    if (path) {
        const auto basename = path.baseName();
        if (basename) {
            g_file_info_set_name(fallback, basename.get());
            g_file_info_set_display_name(fallback, basename.get());
        }
    }
    return GFileInfoPtr{fallback, false};
}

GErrorPtr coreResultToError(const CoreFileOps::Result& result, const char* fallbackMessage) {
    const int code =
        result.error.sysErrno != 0 ? g_io_error_from_errno(result.error.sysErrno) : static_cast<int>(G_IO_ERROR_FAILED);
    const char* message = fallbackMessage;
    if (!result.error.message.empty()) {
        message = result.error.message.c_str();
    }

    GErrorPtr err;
    g_set_error(&err, G_IO_ERROR, code, "%s", message);
    return err;
}
#endif

}  // namespace

UntrashJob::UntrashJob(FilePathList srcPaths) : srcPaths_{std::move(srcPaths)} {
    setCalcProgressUsingSize(false);
}

void UntrashJob::exec() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    setTotalAmount(srcPaths_.size(), srcPaths_.size());
    Q_EMIT preparedToRun();

    for (const auto& srcPath : srcPaths_) {
        if (isCancelled()) {
            break;
        }

        std::string sourceEndpoint;
        CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
        if (!toCoreEndpointPath(srcPath, sourceEndpoint, sourceKind)) {
            GErrorPtr err;
            g_set_error(&err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s", "Unable to encode source path");
            const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
            if (action == ErrorAction::ABORT) {
                cancel();
                return;
            }
            if (action != ErrorAction::RETRY) {
                addFinishedAmount(1, 1);
                continue;
            }
            continue;
        }

        while (!isCancelled()) {
            std::uint64_t baseFinishedBytes = 0;
            std::uint64_t baseFinishedFiles = 0;
            finishedAmount(baseFinishedBytes, baseFinishedFiles);

            CoreFileOps::Request request;
            request.operation = CoreFileOps::Operation::Untrash;
            request.sources = {sourceEndpoint};
            request.conflictPolicy = CoreFileOps::ConflictPolicy::Prompt;
            request.symlinkPolicy.followSymlinks = false;
            request.symlinkPolicy.copyMode = CoreFileOps::SymlinkCopyMode::CopyLinkAsLink;
            request.metadata.preserveOwnership = true;
            request.metadata.preservePermissions = true;
            request.metadata.preserveTimestamps = true;
            request.atomicity.requireAtomicReplace = false;
            request.atomicity.bestEffortAtomicMove = true;
            request.cancelGranularity = CoreFileOps::CancelCheckpointGranularity::PerChunk;
            request.cancellationRequested = [this]() { return isCancelled(); };
            request.linuxSafety.requireOpenat2Resolve = false;
            request.linuxSafety.requireLandlock = false;
            request.linuxSafety.requireSeccomp = false;
            request.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;
            request.routing.defaultBackend = CoreFileOps::Backend::Gio;
            request.routing.sourceKinds = {sourceKind};
            request.routing.sourceBackends = {CoreFileOps::Backend::Gio};

            CoreFileOps::EventHandlers handlers;
            handlers.onProgress = [this, baseFinishedBytes, baseFinishedFiles,
                                   srcPath](const CoreFileOps::ProgressSnapshot& progress) {
                setCurrentFile(toFilePathFromCorePath(progress.currentPath, srcPath));
                setCurrentFileProgress(0, 0);

                const std::uint64_t bytesDone = progress.bytesDone;
                const std::uint64_t filesDone =
                    progress.filesDone > 0 ? static_cast<std::uint64_t>(progress.filesDone) : 0;
                setFinishedAmount(baseFinishedBytes + bytesDone, baseFinishedFiles + filesDone);
            };

            handlers.onConflict = [this, srcPath](const CoreFileOps::ConflictEvent& event) {
                const FilePath eventSource = toFilePathFromCorePath(event.sourcePath, srcPath);
                const FilePath eventDestination = toFilePathFromCorePath(event.destinationPath, FilePath{});

                GFileInfoPtr sourceInfo = queryInfoOrFallback(eventSource, cancellable().get());
                GFileInfoPtr destinationInfo = queryInfoOrFallback(eventDestination, cancellable().get());

                FilePath ignoredNewDestination;
                const FileExistsAction action =
                    askRename(FileInfo{sourceInfo, eventSource}, FileInfo{destinationInfo, eventDestination},
                              ignoredNewDestination);
                switch (action) {
                    case FileOperationJob::OVERWRITE:
                        return CoreFileOps::ConflictResolution::Overwrite;
                    case FileOperationJob::SKIP:
                    case FileOperationJob::SKIP_ERROR:
                        return CoreFileOps::ConflictResolution::Skip;
                    case FileOperationJob::RENAME:
                        return CoreFileOps::ConflictResolution::Rename;
                    case FileOperationJob::CANCEL:
                    default:
                        cancel();
                        return CoreFileOps::ConflictResolution::Abort;
                }
            };

            const CoreFileOps::Result result = CoreFileOps::run(request, handlers);
            if (result.success) {
                const std::uint64_t bytesDone = result.finalProgress.bytesDone;
                const std::uint64_t filesDone =
                    result.finalProgress.filesDone > 0 ? static_cast<std::uint64_t>(result.finalProgress.filesDone) : 0;
                setFinishedAmount(baseFinishedBytes + bytesDone, baseFinishedFiles + filesDone);
                setCurrentFileProgress(0, 0);
                break;
            }

            if (result.cancelled || result.error.sysErrno == ECANCELED) {
                cancel();
                setCurrentFileProgress(0, 0);
                return;
            }

            GErrorPtr err = coreResultToError(result, "Untrash operation failed");
            const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
            if (action == ErrorAction::RETRY) {
                setFinishedAmount(baseFinishedBytes, baseFinishedFiles);
                setCurrentFileProgress(0, 0);
                continue;
            }
            if (action == ErrorAction::ABORT) {
                cancel();
            }
            setCurrentFileProgress(0, 0);
            break;
        }
    }
#else
    // preparing for the job
    FilePathList validSrcPaths;
    FilePathList origPaths;
    for (auto& srcPath : srcPaths_) {
        if (isCancelled()) {
            break;
        }
        GErrorPtr err;
        GFileInfoPtr srcInfo{g_file_query_info(srcPath.gfile().get(), "trash::orig-path",
                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable().get(), &err),
                             false};
        if (srcInfo) {
            const char* orig_path_str = g_file_info_get_attribute_byte_string(srcInfo.get(), "trash::orig-path");
            if (orig_path_str) {
                validSrcPaths.emplace_back(srcPath);
                origPaths.emplace_back(FilePath::fromPathStr(orig_path_str));
            }
            else {
                g_set_error(&err, G_IO_ERROR, G_IO_ERROR_FAILED,
                            tr("Cannot untrash file '%s': original path not known").toUtf8().constData(),
                            g_file_info_get_display_name(srcInfo.get()));
                // FIXME: do we need to retry here?
                emitError(err, ErrorSeverity::MODERATE);
            }
        }
        else {
            // FIXME: do we need to retry here?
            emitError(err);
        }
    }

    // collected original paths of the trashed files
    // use the file transfer job to handle the actual file move
    FileTransferJob fileTransferJob{std::move(validSrcPaths), std::move(origPaths), FileTransferJob::Mode::MOVE};
    // FIXME:
    // I'm not sure why specifying Qt::DirectConnection is needed here since the caller & receiver are in the same
    // thread. :-( However without this, the signals/slots here will cause deadlocks.
    connect(&fileTransferJob, &FileTransferJob::preparedToRun, this, &UntrashJob::preparedToRun, Qt::DirectConnection);
    connect(&fileTransferJob, &FileTransferJob::error, this, &UntrashJob::error, Qt::DirectConnection);
    connect(&fileTransferJob, &FileTransferJob::fileExists, this, &UntrashJob::fileExists, Qt::DirectConnection);

    // cancel the file transfer subjob if the parent job is cancelled
    connect(
        this, &UntrashJob::cancelled, &fileTransferJob,
        [&fileTransferJob]() {
            if (!fileTransferJob.isCancelled()) {
                fileTransferJob.cancel();
            }
        },
        Qt::DirectConnection);

    // cancel the parent job if the file transfer subjob is cancelled
    connect(
        &fileTransferJob, &FileTransferJob::cancelled, this,
        [this]() {
            if (!isCancelled()) {
                cancel();
            }
        },
        Qt::DirectConnection);
    fileTransferJob.run();
#endif
}

}  // namespace Fm
