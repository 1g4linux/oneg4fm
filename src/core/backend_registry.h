/*
 * Backend registry for creating file operation instances
 * src/core/backend_registry.h
 */

#ifndef BACKEND_REGISTRY_H
#define BACKEND_REGISTRY_H

#include <memory>

#include "ifileops.h"
#include "ifoldermodel.h"

namespace Oneg4FM {

class BackendRegistry {
   public:
    static void initDefaults();

    static std::unique_ptr<IFileOps> createFileOps();
    static std::unique_ptr<IFolderModel> createFolderModel(QObject* parent);
};

}  // namespace Oneg4FM

#endif  // BACKEND_REGISTRY_H
