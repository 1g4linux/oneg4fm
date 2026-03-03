#include "deletejob.h"
#include "fileops_bridge_policy.h"
#include "fileinfo_p.h"

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

FilePath toFilePathFromCorePath(const std::string& path, const FilePath& fallback) {
    if (path.empty()) {
        return fallback;
    }

    if (path.find("://") != std::string::npos) {
        return FilePath::fromUri(path.c_str());
    }
    return FilePath::fromLocalPath(path.c_str());
}

bool toCoreEndpointPath(const FilePath& path, std::string& out, CoreFileOps::EndpointKind& endpointKind) {
    if (path.isNative()) {
        endpointKind = CoreFileOps::EndpointKind::NativePath;
        return toNativePath(path, out);
    }

    endpointKind = CoreFileOps::EndpointKind::Uri;
    return toUriPath(path, out);
}

CoreFileOps::Backend backendForRouting(FileOpsBridgePolicy::RoutingClass routingClass) {
    if (routingClass == FileOpsBridgePolicy::RoutingClass::LegacyGio) {
        return CoreFileOps::Backend::Gio;
    }
    return CoreFileOps::Backend::LocalHardened;
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

const char* routingClassName(FileOpsBridgePolicy::RoutingClass routingClass) {
    switch (routingClass) {
        case FileOpsBridgePolicy::RoutingClass::CoreLocal:
            return "CoreLocal";
        case FileOpsBridgePolicy::RoutingClass::LegacyGio:
            return "LegacyGio";
        case FileOpsBridgePolicy::RoutingClass::Unsupported:
            return "Unsupported";
    }
    return "Unknown";
}

std::string describePathForRoutingError(const FilePath& path) {
    if (!path) {
        return "<invalid>";
    }

    const auto localPath = path.localPath();
    if (localPath && localPath.get()[0] != '\0') {
        return localPath.get();
    }

    const auto uri = path.uri();
    if (uri && uri.get()[0] != '\0') {
        return uri.get();
    }
    return "<unresolved>";
}

GErrorPtr unsupportedDeleteRoutingError(const FilePath& path, FileOpsBridgePolicy::RoutingClass routingClass) {
    const std::string routedPath = describePathForRoutingError(path);
    GErrorPtr err;
    g_set_error(&err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                "Refusing legacy local delete fallback: unsafe routing classification (path=%s, pathClass=%s)",
                routedPath.c_str(), routingClassName(routingClass));
    return err;
}
#endif

}  // namespace

bool DeleteJob::deleteFile(const FilePath& path, GFileInfoPtr inf) {
    ErrorAction act = ErrorAction::CONTINUE;
    while (!inf) {
        GErrorPtr err;
        inf = GFileInfoPtr{g_file_query_info(path.gfile().get(), "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                             cancellable().get(), &err),
                           false};
        if (err) {
            act = emitError(err, ErrorSeverity::SEVERE);
            if (act == ErrorAction::ABORT) {
                return false;
            }
            if (act != ErrorAction::RETRY) {
                break;
            }
        }
    }

    // TODO: get parent dir of the current path.
    //       if there is a Fm::Folder object created for it, block the update for the folder temporarily.

    /* currently processed file. */
    setCurrentFile(path);

    if (g_file_info_get_file_type(inf.get()) == G_FILE_TYPE_DIRECTORY) {
        // delete the content of the dir prior to deleting itself
        deleteDirContent(path, inf);
    }

    bool isTrashRoot = false;
    // special handling for trash:///
    if (!path.isNative() && g_strcmp0(path.uriScheme().get(), "trash") == 0) {
        // little trick: basename of trash root is /
        auto basename = path.baseName();
        if (basename && basename[0] == G_DIR_SEPARATOR) {
            isTrashRoot = true;
        }
    }

    bool hasError = false;
    while (!isCancelled()) {
        GErrorPtr err;
        // try to delete the path directly (but don't delete if it's trash:///)
        if (isTrashRoot || g_file_delete(path.gfile().get(), cancellable().get(), &err)) {
            break;
        }
        if (err) {
            // FIXME: error handling
            /* if it's non-empty dir then descent into it then try again */
            /* trash root gives G_IO_ERROR_PERMISSION_DENIED */
            if (err.domain() == G_IO_ERROR && err.code() == G_IO_ERROR_NOT_EMPTY) {
                deleteDirContent(path, inf);
            }
            else if (err.domain() == G_IO_ERROR && err.code() == G_IO_ERROR_PERMISSION_DENIED) {
                /* special case for trash:/// */
                /* FIXME: is there any better way to handle this? */
                auto scheme = path.uriScheme();
                if (g_strcmp0(scheme.get(), "trash") == 0) {
                    break;
                }
            }
            act = emitError(err, ErrorSeverity::MODERATE);
            if (act != ErrorAction::RETRY) {
                hasError = true;
                break;
            }
        }
    }

    addFinishedAmount(g_file_info_get_attribute_uint64(inf.get(), G_FILE_ATTRIBUTE_STANDARD_SIZE), 1);

    return !hasError;
}

bool DeleteJob::deleteDirContent(const FilePath& path, GFileInfoPtr inf) {
    GErrorPtr err;
    GFileEnumeratorPtr enu{g_file_enumerate_children(path.gfile().get(), defaultGFileInfoQueryAttribs,
                                                     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable().get(), &err),
                           false};
    if (!enu) {
        emitError(err, ErrorSeverity::MODERATE);
        return false;
    }

    bool hasError = false;
    while (!isCancelled()) {
        inf = GFileInfoPtr{g_file_enumerator_next_file(enu.get(), cancellable().get(), &err), false};
        if (inf) {
            auto subPath = path.child(g_file_info_get_name(inf.get()));
            if (!deleteFile(subPath, inf)) {
                continue;
            }
        }
        else {
            if (err) {
                emitError(err, ErrorSeverity::MODERATE);
                /* ErrorAction::RETRY is not supported here */
                hasError = true;
            }
            else { /* EOF */
            }
            break;
        }
    }
    g_file_enumerator_close(enu.get(), nullptr, nullptr);
    return !hasError;
}

DeleteJob::DeleteJob(const FilePathList& paths) : paths_{paths} {
    setCalcProgressUsingSize(false);
}

DeleteJob::DeleteJob(FilePathList&& paths) : paths_{paths} {
    setCalcProgressUsingSize(false);
}

DeleteJob::~DeleteJob() {}

void DeleteJob::exec() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    std::uint64_t aggregateTotalBytes = 0;
    std::uint64_t aggregateTotalFiles = 0;
    std::uint64_t aggregateFinishedBytes = 0;
    std::uint64_t aggregateFinishedFiles = 0;
    setTotalAmount(aggregateTotalBytes, aggregateTotalFiles);
#else
    setTotalAmount(0, paths_.size());
#endif
    Q_EMIT preparedToRun();

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    auto runCoreRoutedDelete = [this, &aggregateTotalBytes, &aggregateTotalFiles, &aggregateFinishedBytes,
                                &aggregateFinishedFiles](const FilePath& path,
                                                         FileOpsBridgePolicy::RoutingClass routingClass) -> bool {
        std::string sourceEndpoint;
        CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
        if (!toCoreEndpointPath(path, sourceEndpoint, sourceKind)) {
            return false;
        }

        const CoreFileOps::Backend requestBackend = backendForRouting(routingClass);

        CoreFileOps::DeleteRequest request;
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
        request.common.options.linuxSafety.requireOpenat2Resolve =
            (requestBackend == CoreFileOps::Backend::LocalHardened);
        request.common.options.linuxSafety.requireLandlock = false;
        request.common.options.linuxSafety.requireSeccomp = false;
        request.common.options.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;
        request.common.options.routing.defaultBackend = requestBackend;
        request.common.options.routing.sourceKinds = {sourceKind};
        request.common.options.routing.sourceBackends = {requestBackend};

        CoreFileOps::EventHandlers handlers;
        handlers.onProgress = [this, &aggregateTotalBytes, &aggregateTotalFiles, &aggregateFinishedBytes,
                               &aggregateFinishedFiles, path](const CoreFileOps::ProgressSnapshot& progress) {
            setCurrentFile(toFilePathFromCorePath(progress.currentPath, path));
            setCurrentFileProgress(0, 0);

            const std::uint64_t bytesDone = progress.bytesDone;
            const std::uint64_t filesDone = progress.filesDone > 0 ? static_cast<std::uint64_t>(progress.filesDone) : 0;
            const std::uint64_t bytesTotal = progress.bytesTotal;
            const std::uint64_t filesTotal =
                progress.filesTotal > 0 ? static_cast<std::uint64_t>(progress.filesTotal) : 0;

            setTotalAmount(aggregateTotalBytes + bytesTotal, aggregateTotalFiles + filesTotal);
            setFinishedAmount(aggregateFinishedBytes + bytesDone, aggregateFinishedFiles + filesDone);
        };

        const CoreFileOps::Result result = CoreFileOps::run(request, handlers);
        const std::uint64_t bytesDone = result.finalProgress.bytesDone;
        const std::uint64_t filesDone =
            result.finalProgress.filesDone > 0 ? static_cast<std::uint64_t>(result.finalProgress.filesDone) : 0;
        const std::uint64_t bytesTotal = result.finalProgress.bytesTotal;
        const std::uint64_t filesTotal =
            result.finalProgress.filesTotal > 0 ? static_cast<std::uint64_t>(result.finalProgress.filesTotal) : 0;

        aggregateTotalBytes += bytesTotal;
        aggregateTotalFiles += filesTotal;
        aggregateFinishedBytes += bytesDone;
        aggregateFinishedFiles += filesDone;
        setTotalAmount(aggregateTotalBytes, aggregateTotalFiles);
        setFinishedAmount(aggregateFinishedBytes, aggregateFinishedFiles);
        setCurrentFileProgress(0, 0);

        if (result.success) {
            return true;
        }

        if (result.cancelled || result.error.sysErrno == ECANCELED) {
            cancel();
            return false;
        }

        GErrorPtr err = coreResultToError(result, "Core delete operation failed");
        const ErrorAction action = emitError(err, ErrorSeverity::MODERATE);
        if (action == ErrorAction::ABORT) {
            cancel();
        }
        return false;
    };
#endif

    for (auto& path : paths_) {
        if (isCancelled()) {
            break;
        }

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
        const auto pathRouting = FileOpsBridgePolicy::classifyPathForFileOps(path);
        if (pathRouting == FileOpsBridgePolicy::RoutingClass::Unsupported) {
            GErrorPtr err = unsupportedDeleteRoutingError(path, pathRouting);
            emitError(err, ErrorSeverity::CRITICAL);
            cancel();
            break;
        }

        runCoreRoutedDelete(path, pathRouting);
        continue;
#endif

        deleteFile(path, GFileInfoPtr{nullptr});
    }
}

}  // namespace Fm
