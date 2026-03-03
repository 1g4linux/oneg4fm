#include "trashjob.h"

#include "fileops_bridge_policy.h"

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

    /* FIXME: we shouldn't trash a file already in trash:/// */
    for (auto& path : paths_) {
        if (isCancelled()) {
            break;
        }

        setCurrentFile(path);

        // TODO: get parent dir of the current path.
        //       if there is a Fm::Folder object created for it, block the update for the folder temporarily.

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
        std::string sourceEndpoint;
        CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
        if (!toCoreEndpointPath(path, sourceEndpoint, sourceKind)) {
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

        CoreFileOps::TrashRequest request;
        request.common.sources = {sourceEndpoint};
        request.common.options.symlinkPolicy.followSymlinks = false;
        request.common.options.symlinkPolicy.copyMode = CoreFileOps::SymlinkCopyMode::CopyLinkAsLink;
        request.common.options.metadata.preserveOwnership = true;
        request.common.options.metadata.preservePermissions = true;
        request.common.options.metadata.preserveTimestamps = true;
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
