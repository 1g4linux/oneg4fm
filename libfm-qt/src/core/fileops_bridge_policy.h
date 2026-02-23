#ifndef FM2_FILEOPS_BRIDGE_POLICY_H
#define FM2_FILEOPS_BRIDGE_POLICY_H

#include "filepath.h"

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

}  // namespace Fm::FileOpsBridgePolicy

#endif  // FM2_FILEOPS_BRIDGE_POLICY_H
