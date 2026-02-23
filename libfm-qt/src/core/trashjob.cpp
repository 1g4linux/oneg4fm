#include "trashjob.h"

#include "core/legacy/fm-config.h"
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
namespace CoreFileOps = PCManFM::FileOpsContract;

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

        for (;;) {  // retry the i/o operation on errors
            auto gf = path.gfile();
            bool ret = false;
            // FIXME: do not depend on fm_config
            if (fm_config->no_usb_trash) {
                GMountPtr mnt{g_file_find_enclosing_mount(gf.get(), nullptr, nullptr), false};
                if (mnt) {
                    ret = g_mount_can_unmount(mnt.get()); /* TRUE if it's removable media */
                    if (ret) {
                        unsupportedFiles_.push_back(path);
                        break;  // don't trash the file
                    }
                }
            }

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
                if (action != ErrorAction::RETRY) {
                    break;
                }
                continue;
            }

            CoreFileOps::Request request;
            request.operation = CoreFileOps::Operation::Trash;
            request.sources = {sourceEndpoint};
            request.symlinkPolicy.followSymlinks = false;
            request.symlinkPolicy.copyMode = CoreFileOps::SymlinkCopyMode::CopyLinkAsLink;
            request.metadata.preserveOwnership = true;
            request.metadata.preservePermissions = true;
            request.metadata.preserveTimestamps = true;
            request.cancelGranularity = CoreFileOps::CancelCheckpointGranularity::PerChunk;
            request.cancellationRequested = [this]() { return isCancelled(); };
            request.linuxSafety.requireOpenat2Resolve = false;
            request.linuxSafety.requireLandlock = false;
            request.linuxSafety.requireSeccomp = false;
            request.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;
            request.routing.defaultBackend = CoreFileOps::Backend::Gio;
            request.routing.sourceKinds = {sourceKind};
            request.routing.sourceBackends = {CoreFileOps::Backend::Gio};

            const CoreFileOps::Result result = CoreFileOps::run(request);
            if (result.success) {
                break;
            }

            if (result.cancelled || result.error.sysErrno == ECANCELED) {
                cancel();
                return;
            }

            if (result.error.sysErrno == ENOTSUP) {
                unsupportedFiles_.push_back(path);
                break;
            }

            GErrorPtr err = coreResultToError(result, "Trash operation failed");
            const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
            if (action == ErrorAction::RETRY) {
                continue;
            }
            if (action == ErrorAction::ABORT) {
                cancel();
                return;
            }
            break;
#else
            // move the file to trash
            GErrorPtr err;
            ret = g_file_trash(gf.get(), cancellable().get(), &err);
            if (ret) {  // trash operation succeeded
                break;
            }
            else {  // failed
                // if trashing is not supported by the file system
                if (err.domain() == G_IO_ERROR && err.code() == G_IO_ERROR_NOT_SUPPORTED) {
                    unsupportedFiles_.push_back(path);
                    break;
                }
                else {
                    ErrorAction act = emitError(err, ErrorSeverity::MODERATE);
                    if (act == ErrorAction::RETRY) {
                        err.reset();
                    }
                    else if (act == ErrorAction::ABORT) {
                        cancel();
                        return;
                    }
                    else {
                        break;
                    }
                }
            }
#endif
        }
        addFinishedAmount(1, 1);
    }
}

}  // namespace Fm
