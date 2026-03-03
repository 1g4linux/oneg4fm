#include "deletejob.h"
#include "fileops_bridge_policy.h"
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

    for (auto& path : paths_) {
        if (isCancelled()) {
            break;
        }

        const auto pathRouting = FileOpsBridgePolicy::classifyPathForFileOps(path);
        if (pathRouting == FileOpsBridgePolicy::RoutingClass::Unsupported) {
            GErrorPtr err = unsupportedDeleteRoutingError(path, pathRouting);
            emitError(err, ErrorSeverity::CRITICAL);
            cancel();
            break;
        }

        runCoreRoutedDelete(path, pathRouting);
    }
#else
    GErrorPtr err;
    g_set_error(&err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                "Core file-ops contract unavailable: refusing legacy delete adapter path");
    emitError(err, ErrorSeverity::CRITICAL);
    cancel();
#endif
}

}  // namespace Fm
