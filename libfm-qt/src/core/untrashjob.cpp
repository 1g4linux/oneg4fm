#include "untrashjob.h"
#include "fileops_request_assembly.h"

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

    GErrorPtr countErr;
    if (!FileOpsRequestAssembly::validateRequestPathCount(srcPaths_.size(), "untrash", countErr)) {
        emitError(countErr, ErrorSeverity::CRITICAL);
        cancel();
        return;
    }

    for (const auto& srcPath : srcPaths_) {
        if (isCancelled()) {
            break;
        }

        std::uint64_t baseFinishedBytes = 0;
        std::uint64_t baseFinishedFiles = 0;
        finishedAmount(baseFinishedBytes, baseFinishedFiles);

        CoreFileOps::UntrashRequest request;
        GErrorPtr assembleErr;
        if (!FileOpsRequestAssembly::buildUntrashRequest(
                srcPath, [this]() { return isCancelled(); }, request, assembleErr)) {
            const ErrorAction action = emitError(assembleErr, ErrorSeverity::MODERATE);
            if (action == ErrorAction::ABORT) {
                cancel();
                return;
            }
            addFinishedAmount(1, 1);
            continue;
        }

        CoreFileOps::EventHandlers handlers;
        handlers.onProgress = [this, baseFinishedBytes, baseFinishedFiles,
                               srcPath](const CoreFileOps::ProgressSnapshot& progress) {
            setCurrentFile(FileOpsRequestAssembly::toFilePathFromCorePath(progress.currentPath, srcPath));
            setCurrentFileProgress(0, 0);

            const std::uint64_t bytesDone = progress.bytesDone;
            const std::uint64_t filesDone = progress.filesDone > 0 ? static_cast<std::uint64_t>(progress.filesDone) : 0;
            setFinishedAmount(baseFinishedBytes + bytesDone, baseFinishedFiles + filesDone);
        };

        handlers.onConflict = [this, srcPath](const CoreFileOps::ConflictEvent& event) {
            const FilePath eventSource = FileOpsRequestAssembly::toFilePathFromCorePath(event.sourcePath, srcPath);
            const FilePath eventDestination =
                FileOpsRequestAssembly::toFilePathFromCorePath(event.destinationPath, FilePath{});

            GFileInfoPtr sourceInfo = FileOpsRequestAssembly::makePromptInfoFromPath(eventSource);
            GFileInfoPtr destinationInfo = FileOpsRequestAssembly::makePromptInfoFromPath(eventDestination);

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
