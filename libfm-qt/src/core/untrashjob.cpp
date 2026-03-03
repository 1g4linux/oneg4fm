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

GErrorPtr coreResultToError(const CoreFileOps::OpResult& result, const char* fallbackMessage) {
    const int code = result.diagnostics.sysErrno != 0 ? g_io_error_from_errno(result.diagnostics.sysErrno)
                                                      : static_cast<int>(G_IO_ERROR_FAILED);
    const char* message = fallbackMessage;
    if (!result.diagnostics.message.empty()) {
        message = result.diagnostics.message.c_str();
    }

    const std::string detail = std::string(message) +
                               " [engine_code=" + std::to_string(static_cast<int>(result.diagnostics.code)) +
                               ", step=" + std::to_string(static_cast<int>(result.diagnostics.step)) +
                               ", errno=" + std::to_string(result.diagnostics.sysErrno) + "]";

    GErrorPtr err;
    g_set_error(&err, G_IO_ERROR, code, "%s", detail.c_str());
    return err;
}
#endif

}  // namespace

UntrashJob::UntrashJob(FilePathList srcPaths) : srcPaths_{std::move(srcPaths)} {
    setCalcProgressUsingSize(false);
}

void UntrashJob::exec() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    std::uint64_t aggregateTotalBytes = 0;
    std::uint64_t aggregateTotalFiles = 0;
    std::uint64_t aggregateFinishedBytes = 0;
    std::uint64_t aggregateFinishedFiles = 0;
    setTotalAmount(aggregateTotalBytes, aggregateTotalFiles);
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
        handlers.onProgress = [this, &aggregateTotalBytes, &aggregateTotalFiles, &aggregateFinishedBytes,
                               &aggregateFinishedFiles, srcPath](const CoreFileOps::ProgressSnapshot& progress) {
            setCurrentFile(FileOpsRequestAssembly::toFilePathFromCorePath(progress.currentPath, srcPath));
            setCurrentFileProgress(0, 0);

            const std::uint64_t bytesDone = progress.bytesDone;
            const std::uint64_t filesDone = progress.filesDone > 0 ? static_cast<std::uint64_t>(progress.filesDone) : 0;
            const std::uint64_t bytesTotal = progress.bytesTotal;
            const std::uint64_t filesTotal =
                progress.filesTotal > 0 ? static_cast<std::uint64_t>(progress.filesTotal) : 0;

            setTotalAmount(aggregateTotalBytes + bytesTotal, aggregateTotalFiles + filesTotal);
            setFinishedAmount(aggregateFinishedBytes + bytesDone, aggregateFinishedFiles + filesDone);
        };

        auto promptForConflictAction = [this, srcPath](const std::string& sourcePath,
                                                       const std::string& destinationPath) {
            const FilePath eventSource = FileOpsRequestAssembly::toFilePathFromCorePath(sourcePath, srcPath);
            const FilePath eventDestination =
                FileOpsRequestAssembly::toFilePathFromCorePath(destinationPath, FilePath{});

            GFileInfoPtr sourceInfo = FileOpsRequestAssembly::makePromptInfoFromPath(eventSource);
            GFileInfoPtr destinationInfo = FileOpsRequestAssembly::makePromptInfoFromPath(eventDestination);

            FilePath ignoredNewDestination;
            return askRename(FileInfo{sourceInfo, eventSource}, FileInfo{destinationInfo, eventDestination},
                             ignoredNewDestination);
        };

        bool pendingPromptDecision = false;
        FileExistsAction pendingPromptAction = FileOperationJob::CANCEL;

        CoreFileOps::EventStreamHandlers streamHandlers;
        streamHandlers.onPrompt = [&pendingPromptDecision, &pendingPromptAction,
                                   &promptForConflictAction](const CoreFileOps::PromptEvent& event) {
            if (event.kind != CoreFileOps::PromptKind::ConflictResolution) {
                return;
            }

            pendingPromptAction = promptForConflictAction(event.sourcePath, event.destinationPath);
            pendingPromptDecision = true;
        };

        streamHandlers.onConflict = [this, &pendingPromptDecision, &pendingPromptAction,
                                     &promptForConflictAction](const CoreFileOps::ConflictEvent& event) {
            const FileExistsAction action = pendingPromptDecision
                                                ? pendingPromptAction
                                                : promptForConflictAction(event.sourcePath, event.destinationPath);
            pendingPromptDecision = false;

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

        const CoreFileOps::Result rawResult =
            CoreFileOps::run(CoreFileOps::toRequest(request), handlers, streamHandlers);
        const CoreFileOps::OpResult result = CoreFileOps::toOpResult(rawResult, request.common);

        const std::uint64_t bytesDone = result.counters.bytesDone;
        const std::uint64_t filesDone =
            result.counters.filesDone > 0 ? static_cast<std::uint64_t>(result.counters.filesDone) : 0;
        const std::uint64_t bytesTotal = result.counters.bytesTotal;
        const std::uint64_t filesTotal =
            result.counters.filesTotal > 0 ? static_cast<std::uint64_t>(result.counters.filesTotal) : 0;

        aggregateTotalBytes += bytesTotal;
        aggregateTotalFiles += filesTotal;
        aggregateFinishedBytes += bytesDone;
        aggregateFinishedFiles += filesDone;
        setTotalAmount(aggregateTotalBytes, aggregateTotalFiles);
        setFinishedAmount(aggregateFinishedBytes, aggregateFinishedFiles);
        setCurrentFileProgress(0, 0);

        if (result.status == CoreFileOps::OpStatus::Success) {
            continue;
        }

        if (result.status == CoreFileOps::OpStatus::Cancelled || result.diagnostics.sysErrno == ECANCELED) {
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
