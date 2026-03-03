#ifndef FM2_FILEOPS_BRIDGE_POLICY_H
#define FM2_FILEOPS_BRIDGE_POLICY_H

#include "filepath.h"

#ifndef LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#define LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 0
#endif

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#include "fileoperationjob.h"
#include "../../../src/core/file_ops_contract.h"
#endif

namespace Fm::FileOpsBridgePolicy {

enum class RoutingClass {
    CoreLocal,
    LegacyGio,
    Unsupported,
};

// Classify a path for compatibility routing:
// - CoreLocal: route through src/core file-ops contract
// - LegacyGio: keep using legacy GIO adapter path
// - Unsupported: native-looking path that cannot be classified safely
RoutingClass classifyPathForFileOps(const FilePath& path);

// Convenience helper used by existing callers.
// Core file-ops contract is used only for native local paths on non-remote filesystems.
bool isCoreLocalPathEligible(const FilePath& path);

const char* routingClassName(RoutingClass routingClass);

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
namespace CoreFileOps = Oneg4FM::FileOpsContract;

enum class TransferKind {
    Copy,
    Move,
};

CoreFileOps::Backend toCoreBackend(RoutingClass routingClass);
CoreFileOps::Backend toCoreBackend(RoutingClass sourceRouting, RoutingClass destinationRouting);
CoreFileOps::TransferOperation toCoreTransferOperation(TransferKind transferKind);
CoreFileOps::ConflictResolution toCoreConflictResolution(FileOperationJob::FileExistsAction action,
                                                         bool* mappedOut = nullptr);
#endif

}  // namespace Fm::FileOpsBridgePolicy

#endif  // FM2_FILEOPS_BRIDGE_POLICY_H
