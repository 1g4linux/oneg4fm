#include "trashjob.h"

#include "fileops_bridge_policy.h"
#include "fileops_request_assembly.h"

#include <cerrno>
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

TrashJob::TrashJob(FilePathList paths) : paths_{std::move(paths)} {
    // calculate progress using finished file counts rather than their sizes
    setCalcProgressUsingSize(false);
}

void TrashJob::exec() {
    setTotalAmount(paths_.size(), paths_.size());
    Q_EMIT preparedToRun();

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
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
        CoreFileOps::TrashRequest request;
        GErrorPtr assembleErr;
        if (!FileOpsRequestAssembly::buildTrashRequest(
                path, [this]() { return isCancelled(); }, request, assembleErr)) {
            const ErrorAction action = emitError(assembleErr, ErrorSeverity::MODERATE);
            if (action == ErrorAction::ABORT) {
                cancel();
                return;
            }
            addFinishedAmount(1, 1);
            continue;
        }

        const CoreFileOps::Result result = CoreFileOps::run(request);
        if (result.success) {
            addFinishedAmount(1, 1);
            continue;
        }

        if (result.cancelled || result.error.sysErrno == ECANCELED) {
            cancel();
            return;
        }

        if (result.error.sysErrno == ENOTSUP) {
            unsupportedFiles_.push_back(path);
            addFinishedAmount(1, 1);
            continue;
        }

        GErrorPtr err = coreResultToError(result, "Trash operation failed");
        const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
        if (action == ErrorAction::ABORT) {
            cancel();
            return;
        }
        addFinishedAmount(1, 1);
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
