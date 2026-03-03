#include "deletejob.h"
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
    GErrorPtr countErr;
    if (!FileOpsRequestAssembly::validateRequestPathCount(paths_.size(), "delete", countErr)) {
        emitError(countErr, ErrorSeverity::CRITICAL);
        cancel();
        return;
    }

    auto runCoreRoutedDelete = [this, &aggregateTotalBytes, &aggregateTotalFiles, &aggregateFinishedBytes,
                                &aggregateFinishedFiles](const FilePath& path,
                                                         FileOpsBridgePolicy::RoutingClass routingClass) -> bool {
        CoreFileOps::DeleteRequest request;
        GErrorPtr assembleErr;
        if (!FileOpsRequestAssembly::buildDeleteRequest(
                path, routingClass, [this]() { return isCancelled(); }, request, assembleErr)) {
            const ErrorAction action = emitError(assembleErr, ErrorSeverity::MODERATE);
            if (action == ErrorAction::ABORT) {
                cancel();
            }
            return false;
        }

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

        const CoreFileOps::OpResult result = CoreFileOps::runOp(request, handlers);
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
            return true;
        }

        if (result.status == CoreFileOps::OpStatus::Cancelled || result.diagnostics.sysErrno == ECANCELED) {
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
