#include "untrashjob.h"

#ifndef LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#define LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 0
#endif

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
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

GFileInfoPtr makePromptInfoFromPath(const FilePath& path) {
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
            addFinishedAmount(1, 1);
            continue;
        }

        std::uint64_t baseFinishedBytes = 0;
        std::uint64_t baseFinishedFiles = 0;
        finishedAmount(baseFinishedBytes, baseFinishedFiles);

        CoreFileOps::UntrashRequest request;
        request.common.sources = {sourceEndpoint};
        request.common.policy.conflictPolicy = CoreFileOps::ConflictPolicy::Prompt;
        request.common.options.symlinkPolicy.followSymlinks = false;
        request.common.options.symlinkPolicy.copyMode = CoreFileOps::SymlinkCopyMode::CopyLinkAsLink;
        request.common.options.metadata.preserveOwnership = true;
        request.common.options.metadata.preservePermissions = true;
        request.common.options.metadata.preserveTimestamps = true;
        request.common.options.atomicity.requireAtomicReplace = false;
        request.common.options.atomicity.bestEffortAtomicMove = true;
        request.common.options.cancelGranularity = CoreFileOps::CancelCheckpointGranularity::PerChunk;
        request.common.cancellationRequested = [this, cancelHandle = request.common.cancelHandle]() mutable {
            if (isCancelled()) {
                cancelHandle.cancel();
            }
            return cancelHandle.isCancelled();
        };
        request.common.options.linuxSafety.requireOpenat2Resolve = false;
        request.common.options.linuxSafety.requireLandlock = false;
        request.common.options.linuxSafety.requireSeccomp = false;
        request.common.options.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;
        request.common.options.routing.defaultBackend = CoreFileOps::Backend::Gio;
        request.common.options.routing.sourceKinds = {sourceKind};
        request.common.options.routing.sourceBackends = {CoreFileOps::Backend::Gio};

        CoreFileOps::EventHandlers handlers;
        handlers.onProgress = [this, baseFinishedBytes, baseFinishedFiles,
                               srcPath](const CoreFileOps::ProgressSnapshot& progress) {
            setCurrentFile(toFilePathFromCorePath(progress.currentPath, srcPath));
            setCurrentFileProgress(0, 0);

            const std::uint64_t bytesDone = progress.bytesDone;
            const std::uint64_t filesDone = progress.filesDone > 0 ? static_cast<std::uint64_t>(progress.filesDone) : 0;
            setFinishedAmount(baseFinishedBytes + bytesDone, baseFinishedFiles + filesDone);
        };

        handlers.onConflict = [this, srcPath](const CoreFileOps::ConflictEvent& event) {
            const FilePath eventSource = toFilePathFromCorePath(event.sourcePath, srcPath);
            const FilePath eventDestination = toFilePathFromCorePath(event.destinationPath, FilePath{});

            GFileInfoPtr sourceInfo = makePromptInfoFromPath(eventSource);
            GFileInfoPtr destinationInfo = makePromptInfoFromPath(eventDestination);

            FilePath ignoredNewDestination;
            const FileExistsAction action = askRename(
                FileInfo{sourceInfo, eventSource}, FileInfo{destinationInfo, eventDestination}, ignoredNewDestination);
            switch (action) {
                case FileOperationJob::OVERWRITE:
                    return CoreFileOps::ConflictResolution::Overwrite;
                case FileOperationJob::OVERWRITE_ALL:
                    return CoreFileOps::ConflictResolution::OverwriteAll;
                case FileOperationJob::SKIP:
                case FileOperationJob::SKIP_ERROR:
                    return CoreFileOps::ConflictResolution::Skip;
                case FileOperationJob::SKIP_ALL:
                    return CoreFileOps::ConflictResolution::SkipAll;
                case FileOperationJob::RENAME:
                    return CoreFileOps::ConflictResolution::Rename;
                case FileOperationJob::RENAME_ALL:
                    return CoreFileOps::ConflictResolution::RenameAll;
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
            continue;
        }

        if (result.cancelled || result.error.sysErrno == ECANCELED) {
            cancel();
            setCurrentFileProgress(0, 0);
            return;
        }

        GErrorPtr err = coreResultToError(result, "Untrash operation failed");
        const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
        if (action == ErrorAction::ABORT) {
            cancel();
            return;
        }
        setCurrentFileProgress(0, 0);
    }
#else
    GErrorPtr err;
    g_set_error(&err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                "Core file-ops contract unavailable: refusing legacy untrash adapter path");
    emitError(err, ErrorSeverity::CRITICAL);
    cancel();
    return;
#endif
}

}  // namespace Fm
