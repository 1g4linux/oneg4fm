#include "fileops_bridge_policy.h"

#include "gioptrs.h"

namespace Fm::FileOpsBridgePolicy {
namespace {

bool hasFileScheme(const FilePath& path) {
    const auto scheme = path.uriScheme();
    if (!scheme || scheme.get()[0] == '\0') {
        return true;
    }
    return g_ascii_strcasecmp(scheme.get(), "file") == 0;
}

bool queryFilesystemRemote(const FilePath& path, bool& remoteOut) {
    GErrorPtr err;
    GFileInfoPtr info{
        g_file_query_filesystem_info(path.gfile().get(), G_FILE_ATTRIBUTE_FILESYSTEM_REMOTE, nullptr, &err), false};
    if (!info) {
        return false;
    }
    remoteOut = g_file_info_get_attribute_boolean(info.get(), G_FILE_ATTRIBUTE_FILESYSTEM_REMOTE);
    return true;
}

}  // namespace

bool isCoreLocalPathEligible(const FilePath& path) {
    if (!path || !path.isNative() || !hasFileScheme(path)) {
        return false;
    }

    const auto localPath = path.localPath();
    if (!localPath || localPath.get()[0] == '\0') {
        return false;
    }

    // Walk up until filesystem info is available. Destination targets may not exist yet.
    FilePath probe = path;
    for (int depth = 0; probe && depth < 64; ++depth) {
        bool isRemote = false;
        if (queryFilesystemRemote(probe, isRemote)) {
            return !isRemote;
        }
        probe = probe.parent();
    }

    return false;
}

}  // namespace Fm::FileOpsBridgePolicy
