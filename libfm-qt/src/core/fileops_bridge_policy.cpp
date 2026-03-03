#include "fileops_bridge_policy.h"

namespace Fm::FileOpsBridgePolicy {
namespace {

bool hasFileScheme(const FilePath& path) {
    const auto scheme = path.uriScheme();
    if (!scheme || scheme.get()[0] == '\0') {
        return true;
    }
    return g_ascii_strcasecmp(scheme.get(), "file") == 0;
}

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

}  // namespace Fm::FileOpsBridgePolicy
