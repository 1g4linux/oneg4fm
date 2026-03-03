#ifndef FM2_FILEOPS_REQUEST_ASSEMBLY_H
#define FM2_FILEOPS_REQUEST_ASSEMBLY_H

#include "fileops_bridge_policy.h"
#include "filepath.h"
#include "gioptrs.h"

#include <cstddef>
#include <functional>
#include <string>

#ifndef LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#define LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT 0
#endif

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
#include "../../../src/core/file_ops_contract.h"
#endif

namespace Fm::FileOpsRequestAssembly {

inline constexpr std::size_t kMaxPathsPerRequest = 4096;

bool validateRequestPathCount(std::size_t pathCount, const char* operationName, GErrorPtr& errorOut);

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
namespace CoreFileOps = Oneg4FM::FileOpsContract;
using TransferKind = FileOpsBridgePolicy::TransferKind;

FilePath toFilePathFromCorePath(const std::string& path, const FilePath& fallback);
GFileInfoPtr makePromptInfoFromPath(const FilePath& path);

bool buildTransferRequest(const FilePath& sourcePath,
                          const FilePath& destinationPath,
                          TransferKind transferKind,
                          FileOpsBridgePolicy::RoutingClass sourceRouting,
                          FileOpsBridgePolicy::RoutingClass destinationRouting,
                          const std::function<bool()>& cancelRequested,
                          CoreFileOps::TransferRequest& requestOut,
                          GErrorPtr& errorOut);

bool buildDeleteRequest(const FilePath& sourcePath,
                        FileOpsBridgePolicy::RoutingClass routingClass,
                        const std::function<bool()>& cancelRequested,
                        CoreFileOps::DeleteRequest& requestOut,
                        GErrorPtr& errorOut);

bool buildTrashRequest(const FilePath& sourcePath,
                       const std::function<bool()>& cancelRequested,
                       CoreFileOps::TrashRequest& requestOut,
                       GErrorPtr& errorOut);

bool buildUntrashRequest(const FilePath& sourcePath,
                         const std::function<bool()>& cancelRequested,
                         CoreFileOps::UntrashRequest& requestOut,
                         GErrorPtr& errorOut);
#endif

}  // namespace Fm::FileOpsRequestAssembly

#endif  // FM2_FILEOPS_REQUEST_ASSEMBLY_H
