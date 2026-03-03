#include "trashjob.h"

#include "fileops_bridge_policy.h"
#include "fileops_request_assembly.h"

#include <cerrno>
#include <cstdint>
#include <string>

#ifndef LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#define LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 0
#endif

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#include "../../../src/core/file_ops_contract.h"
#endif

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

TrashJob::TrashJob(FilePathList paths) : paths_{std::move(paths)} {
    // calculate progress using finished file counts rather than their sizes
    setCalcProgressUsingSize(false);
}

void TrashJob::exec() {
    setTotalAmount(paths_.size(), paths_.size());
    Q_EMIT preparedToRun();

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    std::uint64_t aggregateTotalBytes = 0;
    std::uint64_t aggregateTotalFiles = 0;
    std::uint64_t aggregateFinishedBytes = 0;
    std::uint64_t aggregateFinishedFiles = 0;
    setTotalAmount(aggregateTotalBytes, aggregateTotalFiles);

    GErrorPtr countErr;
    if (!FileOpsRequestAssembly::validateRequestPathCount(paths_.size(), "trash", countErr)) {
        emitError(countErr, ErrorSeverity::CRITICAL);
        cancel();
        return;
    }
#endif

    /* FIXME: we shouldn't trash a file already in trash:/// */
    for (auto& path : paths_) {
        if (isCancelled()) {
            break;
        }

        setCurrentFile(path);

        // TODO: get parent dir of the current path.
        //       if there is a Fm::Folder object created for it, block the update for the folder temporarily.

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
        CoreFileOps::EventHandlers handlers;
        handlers.onProgress = [this, &aggregateTotalBytes, &aggregateTotalFiles, &aggregateFinishedBytes,
                               &aggregateFinishedFiles, path](const CoreFileOps::ProgressSnapshot& progress) {
            setCurrentFile(FileOpsRequestAssembly::toFilePathFromCorePath(progress.currentPath, path));
            setCurrentFileProgress(0, 0);

            const std::uint64_t bytesDone = progress.bytesDone;
            const std::uint64_t filesDone = progress.filesDone > 0 ? static_cast<std::uint64_t>(progress.filesDone) : 0;
            const std::uint64_t bytesTotal = progress.bytesTotal;
            const std::uint64_t filesTotal =
                progress.filesTotal > 0 ? static_cast<std::uint64_t>(progress.filesTotal) : 0;

            setTotalAmount(aggregateTotalBytes + bytesTotal, aggregateTotalFiles + filesTotal);
            setFinishedAmount(aggregateFinishedBytes + bytesDone, aggregateFinishedFiles + filesDone);
        };

        CoreFileOps::TrashRequest request;
        GErrorPtr assembleErr;
        if (!FileOpsRequestAssembly::buildTrashRequest(
                path, [this]() { return isCancelled(); }, request, assembleErr)) {
            const ErrorAction action = emitError(assembleErr, ErrorSeverity::MODERATE);
            if (action == ErrorAction::ABORT) {
                cancel();
                return;
            }
            aggregateTotalFiles += 1;
            aggregateFinishedFiles += 1;
            setTotalAmount(aggregateTotalBytes, aggregateTotalFiles);
            setFinishedAmount(aggregateFinishedBytes, aggregateFinishedFiles);
            continue;
        }

        const CoreFileOps::OpResult result = CoreFileOps::runOp(request, handlers);
        std::uint64_t bytesDone = result.counters.bytesDone;
        std::uint64_t filesDone =
            result.counters.filesDone > 0 ? static_cast<std::uint64_t>(result.counters.filesDone) : 0;
        std::uint64_t bytesTotal = result.counters.bytesTotal;
        std::uint64_t filesTotal =
            result.counters.filesTotal > 0 ? static_cast<std::uint64_t>(result.counters.filesTotal) : 0;
        if (bytesTotal == 0 && filesTotal == 0) {
            filesTotal = 1;
            if (result.status != CoreFileOps::OpStatus::Cancelled) {
                filesDone = 1;
            }
        }

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
            return;
        }

        if (result.diagnostics.sysErrno == ENOTSUP) {
            unsupportedFiles_.push_back(path);
            continue;
        }

        GErrorPtr err = coreResultToError(result, "Trash operation failed");
        const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
        if (action == ErrorAction::ABORT) {
            cancel();
            return;
        }
#else
        GErrorPtr err;
        g_set_error(&err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "Core file-ops contract unavailable: refusing legacy trash adapter path");
        emitError(err, ErrorSeverity::CRITICAL);
        cancel();
        return;
#endif
    }
}

}  // namespace Fm
