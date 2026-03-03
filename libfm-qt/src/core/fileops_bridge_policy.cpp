#include "fileops_bridge_policy.h"

#include <array>

namespace Fm::FileOpsBridgePolicy {
namespace {

bool hasFileScheme(const FilePath& path) {
    const auto scheme = path.uriScheme();
    if (!scheme || scheme.get()[0] == '\0') {
        return true;
    }
    return g_ascii_strcasecmp(scheme.get(), "file") == 0;
}

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
struct RoutingBackendEntry {
    RoutingClass routingClass;
    CoreFileOps::Backend backend;
};

constexpr std::array<RoutingBackendEntry, 3> kRoutingBackendMap{{
    {RoutingClass::CoreLocal, CoreFileOps::Backend::LocalHardened},
    {RoutingClass::LegacyGio, CoreFileOps::Backend::Gio},
    {RoutingClass::Unsupported, CoreFileOps::Backend::LocalHardened},
}};

struct RoutingPairBackendEntry {
    RoutingClass sourceRouting;
    RoutingClass destinationRouting;
    CoreFileOps::Backend backend;
};

constexpr std::array<RoutingPairBackendEntry, 9> kRoutingPairBackendMap{{
    {RoutingClass::CoreLocal, RoutingClass::CoreLocal, CoreFileOps::Backend::LocalHardened},
    {RoutingClass::CoreLocal, RoutingClass::LegacyGio, CoreFileOps::Backend::Gio},
    {RoutingClass::CoreLocal, RoutingClass::Unsupported, CoreFileOps::Backend::LocalHardened},
    {RoutingClass::LegacyGio, RoutingClass::CoreLocal, CoreFileOps::Backend::Gio},
    {RoutingClass::LegacyGio, RoutingClass::LegacyGio, CoreFileOps::Backend::Gio},
    {RoutingClass::LegacyGio, RoutingClass::Unsupported, CoreFileOps::Backend::Gio},
    {RoutingClass::Unsupported, RoutingClass::CoreLocal, CoreFileOps::Backend::LocalHardened},
    {RoutingClass::Unsupported, RoutingClass::LegacyGio, CoreFileOps::Backend::Gio},
    {RoutingClass::Unsupported, RoutingClass::Unsupported, CoreFileOps::Backend::LocalHardened},
}};

struct TransferOperationEntry {
    TransferKind transferKind;
    CoreFileOps::TransferOperation transferOperation;
};

constexpr std::array<TransferOperationEntry, 2> kTransferOperationMap{{
    {TransferKind::Copy, CoreFileOps::TransferOperation::Copy},
    {TransferKind::Move, CoreFileOps::TransferOperation::Move},
}};

struct ConflictResolutionEntry {
    FileOperationJob::FileExistsAction action;
    CoreFileOps::ConflictResolution resolution;
};

constexpr std::array<ConflictResolutionEntry, 8> kConflictResolutionMap{{
    {FileOperationJob::CANCEL, CoreFileOps::ConflictResolution::Abort},
    {FileOperationJob::OVERWRITE, CoreFileOps::ConflictResolution::Overwrite},
    {FileOperationJob::OVERWRITE_ALL, CoreFileOps::ConflictResolution::OverwriteAll},
    {FileOperationJob::RENAME, CoreFileOps::ConflictResolution::Rename},
    {FileOperationJob::RENAME_ALL, CoreFileOps::ConflictResolution::RenameAll},
    {FileOperationJob::SKIP, CoreFileOps::ConflictResolution::Skip},
    {FileOperationJob::SKIP_ERROR, CoreFileOps::ConflictResolution::Skip},
    {FileOperationJob::SKIP_ALL, CoreFileOps::ConflictResolution::SkipAll},
}};
#endif

}  // namespace

RoutingClass classifyPathForFileOps(const FilePath& path) {
    if (!path) {
        return RoutingClass::Unsupported;
    }

    if (!path.isNative() || !hasFileScheme(path)) {
        return RoutingClass::LegacyGio;
    }

    const auto localPath = path.localPath();
    if (!localPath || localPath.get()[0] == '\0') {
        return RoutingClass::Unsupported;
    }

    return RoutingClass::CoreLocal;
}

bool isCoreLocalPathEligible(const FilePath& path) {
    return classifyPathForFileOps(path) == RoutingClass::CoreLocal;
}

const char* routingClassName(RoutingClass routingClass) {
    switch (routingClass) {
        case RoutingClass::CoreLocal:
            return "CoreLocal";
        case RoutingClass::LegacyGio:
            return "LegacyGio";
        case RoutingClass::Unsupported:
            return "Unsupported";
    }
    return "Unknown";
}

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
CoreFileOps::Backend toCoreBackend(RoutingClass routingClass) {
    for (const auto& entry : kRoutingBackendMap) {
        if (entry.routingClass == routingClass) {
            return entry.backend;
        }
    }
    return CoreFileOps::Backend::LocalHardened;
}

CoreFileOps::Backend toCoreBackend(RoutingClass sourceRouting, RoutingClass destinationRouting) {
    for (const auto& entry : kRoutingPairBackendMap) {
        if (entry.sourceRouting == sourceRouting && entry.destinationRouting == destinationRouting) {
            return entry.backend;
        }
    }
    return CoreFileOps::Backend::LocalHardened;
}

CoreFileOps::TransferOperation toCoreTransferOperation(TransferKind transferKind) {
    for (const auto& entry : kTransferOperationMap) {
        if (entry.transferKind == transferKind) {
            return entry.transferOperation;
        }
    }
    return CoreFileOps::TransferOperation::Copy;
}

CoreFileOps::ConflictResolution toCoreConflictResolution(FileOperationJob::FileExistsAction action, bool* mappedOut) {
    for (const auto& entry : kConflictResolutionMap) {
        if (entry.action == action) {
            if (mappedOut != nullptr) {
                *mappedOut = true;
            }
            return entry.resolution;
        }
    }

    if (mappedOut != nullptr) {
        *mappedOut = false;
    }
    return CoreFileOps::ConflictResolution::Abort;
}
#endif

}  // namespace Fm::FileOpsBridgePolicy
