#ifndef FM2_FILEOPS_BRIDGE_POLICY_H
#define FM2_FILEOPS_BRIDGE_POLICY_H

#include "filepath.h"

namespace Fm::FileOpsBridgePolicy {

// Core file-ops contract is used only for native local paths on non-remote filesystems.
bool isCoreLocalPathEligible(const FilePath& path);

}  // namespace Fm::FileOpsBridgePolicy

#endif  // FM2_FILEOPS_BRIDGE_POLICY_H
