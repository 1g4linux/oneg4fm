#include "fileops_request_assembly.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <string>
#include <sys/stat.h>

namespace Fm::FileOpsRequestAssembly {

bool validateRequestPathCount(std::size_t pathCount, const char* operationName, GErrorPtr& errorOut) {
    if (pathCount <= kMaxPathsPerRequest) {
        return true;
    }

    const char* name = operationName ? operationName : "file operation";
    g_set_error(&errorOut, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s request has too many paths (%zu > %zu)", name,
                pathCount, kMaxPathsPerRequest);
    return false;
}

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT

namespace {

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

bool toCoreEndpointPath(const FilePath& path,
                        std::string& out,
                        CoreFileOps::EndpointKind& endpointKind,
                        const char* role,
                        GErrorPtr& errorOut) {
    if (!path) {
        g_set_error(&errorOut, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s path is invalid", role);
        return false;
    }

    const bool isNativePath = path.isNative();
    endpointKind = isNativePath ? CoreFileOps::EndpointKind::NativePath : CoreFileOps::EndpointKind::Uri;
    const bool ok = isNativePath ? toNativePath(path, out) : toUriPath(path, out);
    if (ok) {
        return true;
    }

    g_set_error(&errorOut, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unable to encode %s path", role);
    return false;
}

CoreFileOps::SourceSnapshot captureSourceSnapshot(const FilePath& sourcePath) {
    CoreFileOps::SourceSnapshot snapshot;
    if (!sourcePath.isNative()) {
        return snapshot;
    }

    const auto localPath = sourcePath.localPath();
    if (!localPath || localPath.get()[0] == '\0') {
        return snapshot;
    }

    struct stat st{};
    if (::lstat(localPath.get(), &st) != 0) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.device = static_cast<std::uint64_t>(st.st_dev);
    snapshot.inode = static_cast<std::uint64_t>(st.st_ino);
    snapshot.size = static_cast<std::uint64_t>(st.st_size);
    snapshot.mtimeSec = static_cast<std::int64_t>(st.st_mtim.tv_sec);
    snapshot.mtimeNsec = static_cast<std::int64_t>(st.st_mtim.tv_nsec);
    return snapshot;
}

std::string snapshotSummary(const CoreFileOps::SourceSnapshot& snapshot) {
    if (!snapshot.available) {
        return "snapshot=unavailable";
    }

    return "snapshot=dev:" + std::to_string(snapshot.device) + ",ino:" + std::to_string(snapshot.inode) +
           ",size:" + std::to_string(snapshot.size) + ",mtime:" + std::to_string(snapshot.mtimeSec) + "." +
           std::to_string(snapshot.mtimeNsec);
}

std::string nextOperationId(const char* prefix) {
    static std::atomic<std::uint64_t> seq{0};
    const std::uint64_t id = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    std::string opId = prefix ? std::string(prefix) : std::string("libfmqt-op");
    opId += "-";
    opId += std::to_string(id);
    return opId;
}

void applyCommonDefaults(CoreFileOps::RequestCommon& common, CoreFileOps::Backend backend, bool requireOpenat2Resolve) {
    common.options.symlinkPolicy.followSymlinks = false;
    common.options.symlinkPolicy.copyMode = CoreFileOps::SymlinkCopyMode::CopyLinkAsLink;
    common.options.metadata.preserveOwnership = true;
    common.options.metadata.preservePermissions = true;
    common.options.metadata.preserveTimestamps = true;
    common.options.atomicity.requireAtomicReplace = false;
    common.options.atomicity.bestEffortAtomicMove = true;
    common.options.cancelGranularity = CoreFileOps::CancelCheckpointGranularity::PerChunk;
    common.options.linuxSafety.requireOpenat2Resolve = requireOpenat2Resolve;
    common.options.linuxSafety.requireLandlock = false;
    common.options.linuxSafety.requireSeccomp = false;
    common.options.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;
    common.options.routing.defaultBackend = backend;
}

void applyCancellationBridge(CoreFileOps::RequestCommon& common, const std::function<bool()>& cancelRequested) {
    common.cancellationRequested = [cancelRequested, cancelHandle = common.cancelHandle]() mutable {
        if (cancelRequested && cancelRequested()) {
            cancelHandle.cancel();
        }
        return cancelHandle.isCancelled();
    };
}

void applyIdentityAndSnapshot(CoreFileOps::RequestCommon& common, const char* opPrefix, const FilePath& sourcePath) {
    const CoreFileOps::SourceSnapshot snapshot = captureSourceSnapshot(sourcePath);
    common.sourceSnapshots = {snapshot};
    common.opId = nextOperationId(opPrefix);
    common.uiContext.initiator = std::string(opPrefix) + ";" + snapshotSummary(snapshot);
}

}  // namespace

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
    GFileInfo* info = g_file_info_new();
    GFileType fileType = G_FILE_TYPE_UNKNOWN;
    const char* iconName = "text-x-generic";

    if (path) {
        const auto basename = path.baseName();
        if (basename) {
            g_file_info_set_name(info, basename.get());
            g_file_info_set_display_name(info, basename.get());
        }

        if (path.isNative()) {
            const auto localPath = path.localPath();
            if (localPath && localPath.get()[0] != '\0') {
                struct stat st{};
                if (::lstat(localPath.get(), &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        fileType = G_FILE_TYPE_DIRECTORY;
                        iconName = "folder";
                    }
                    else if (S_ISLNK(st.st_mode)) {
                        fileType = G_FILE_TYPE_SYMBOLIC_LINK;
                        iconName = "emblem-symbolic-link";
                    }
                    else if (S_ISREG(st.st_mode)) {
                        fileType = G_FILE_TYPE_REGULAR;
                    }
                    else {
                        fileType = G_FILE_TYPE_SPECIAL;
                    }
                }
            }
        }
    }

    g_file_info_set_file_type(info, fileType);
    GIcon* icon = g_themed_icon_new(iconName);
    g_file_info_set_icon(info, icon);
    g_object_unref(icon);
    return GFileInfoPtr{info, false};
}

bool buildTransferRequest(const FilePath& sourcePath,
                          const FilePath& destinationPath,
                          TransferKind transferKind,
                          FileOpsBridgePolicy::RoutingClass sourceRouting,
                          FileOpsBridgePolicy::RoutingClass destinationRouting,
                          const std::function<bool()>& cancelRequested,
                          CoreFileOps::TransferRequest& requestOut,
                          GErrorPtr& errorOut) {
    requestOut = CoreFileOps::TransferRequest{};

    std::string sourceEndpoint;
    CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
    if (!toCoreEndpointPath(sourcePath, sourceEndpoint, sourceKind, "source", errorOut)) {
        return false;
    }

    std::string destinationEndpoint;
    CoreFileOps::EndpointKind destinationKind = CoreFileOps::EndpointKind::NativePath;
    if (!toCoreEndpointPath(destinationPath, destinationEndpoint, destinationKind, "destination", errorOut)) {
        return false;
    }

    const FilePath destinationDir = destinationPath.parent();
    if (!destinationDir) {
        g_set_error(&errorOut, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s",
                    "Destination parent directory is invalid");
        return false;
    }

    std::string destinationDirEndpoint;
    CoreFileOps::EndpointKind destinationDirKind = CoreFileOps::EndpointKind::NativePath;
    if (!toCoreEndpointPath(destinationDir, destinationDirEndpoint, destinationDirKind, "destination parent",
                            errorOut)) {
        return false;
    }

    const CoreFileOps::Backend backend = FileOpsBridgePolicy::toCoreBackend(sourceRouting, destinationRouting);
    requestOut.transferOperation = FileOpsBridgePolicy::toCoreTransferOperation(transferKind);
    requestOut.common.sources = {sourceEndpoint};
    requestOut.common.destination.targetDir = destinationDirEndpoint;
    requestOut.common.destination.mappingMode = CoreFileOps::DestinationMappingMode::ExplicitPerSource;
    requestOut.common.destination.explicitTargets = {destinationEndpoint};
    requestOut.common.policy.conflictPolicy = CoreFileOps::ConflictPolicy::Prompt;
    applyCommonDefaults(requestOut.common, backend, backend == CoreFileOps::Backend::LocalHardened);
    requestOut.common.options.routing.sourceKinds = {sourceKind};
    requestOut.common.options.routing.sourceBackends = {backend};
    requestOut.common.options.routing.destinationKind = destinationKind;
    requestOut.common.options.routing.destinationBackend = backend;
    applyCancellationBridge(requestOut.common, cancelRequested);
    applyIdentityAndSnapshot(requestOut.common,
                             transferKind == TransferKind::Move ? "libfmqt-transfer-move" : "libfmqt-transfer-copy",
                             sourcePath);
    return true;
}

bool buildDeleteRequest(const FilePath& sourcePath,
                        FileOpsBridgePolicy::RoutingClass routingClass,
                        const std::function<bool()>& cancelRequested,
                        CoreFileOps::DeleteRequest& requestOut,
                        GErrorPtr& errorOut) {
    requestOut = CoreFileOps::DeleteRequest{};

    std::string sourceEndpoint;
    CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
    if (!toCoreEndpointPath(sourcePath, sourceEndpoint, sourceKind, "source", errorOut)) {
        return false;
    }

    const CoreFileOps::Backend backend = FileOpsBridgePolicy::toCoreBackend(routingClass);
    requestOut.common.sources = {sourceEndpoint};
    applyCommonDefaults(requestOut.common, backend, backend == CoreFileOps::Backend::LocalHardened);
    requestOut.common.options.routing.sourceKinds = {sourceKind};
    requestOut.common.options.routing.sourceBackends = {backend};
    applyCancellationBridge(requestOut.common, cancelRequested);
    applyIdentityAndSnapshot(requestOut.common, "libfmqt-delete", sourcePath);
    return true;
}

bool buildTrashRequest(const FilePath& sourcePath,
                       const std::function<bool()>& cancelRequested,
                       CoreFileOps::TrashRequest& requestOut,
                       GErrorPtr& errorOut) {
    requestOut = CoreFileOps::TrashRequest{};

    std::string sourceEndpoint;
    CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
    if (!toCoreEndpointPath(sourcePath, sourceEndpoint, sourceKind, "source", errorOut)) {
        return false;
    }

    requestOut.common.sources = {sourceEndpoint};
    applyCommonDefaults(requestOut.common, CoreFileOps::Backend::Gio, false);
    requestOut.common.options.routing.sourceKinds = {sourceKind};
    requestOut.common.options.routing.sourceBackends = {CoreFileOps::Backend::Gio};
    applyCancellationBridge(requestOut.common, cancelRequested);
    applyIdentityAndSnapshot(requestOut.common, "libfmqt-trash", sourcePath);
    return true;
}

bool buildUntrashRequest(const FilePath& sourcePath,
                         const std::function<bool()>& cancelRequested,
                         CoreFileOps::UntrashRequest& requestOut,
                         GErrorPtr& errorOut) {
    requestOut = CoreFileOps::UntrashRequest{};

    std::string sourceEndpoint;
    CoreFileOps::EndpointKind sourceKind = CoreFileOps::EndpointKind::NativePath;
    if (!toCoreEndpointPath(sourcePath, sourceEndpoint, sourceKind, "source", errorOut)) {
        return false;
    }

    requestOut.common.sources = {sourceEndpoint};
    requestOut.common.policy.conflictPolicy = CoreFileOps::ConflictPolicy::Prompt;
    applyCommonDefaults(requestOut.common, CoreFileOps::Backend::Gio, false);
    requestOut.common.options.routing.sourceKinds = {sourceKind};
    requestOut.common.options.routing.sourceBackends = {CoreFileOps::Backend::Gio};
    applyCancellationBridge(requestOut.common, cancelRequested);
    applyIdentityAndSnapshot(requestOut.common, "libfmqt-untrash", sourcePath);
    return true;
}

#endif

}  // namespace Fm::FileOpsRequestAssembly
