/*
 * Backend registry implementation
 * src/core/backend_registry.cpp
 */

#include "backend_registry.h"

#include <QDebug>
#include <memory>

#include "../backends/qt/qt_fileops.h"
#include "../backends/qt/qt_foldermodel.h"

namespace Oneg4FM {

void BackendRegistry::initDefaults() {
    qDebug() << "BackendRegistry initialized";
}

std::unique_ptr<IFileOps> BackendRegistry::createFileOps() {
    return std::make_unique<QtFileOps>();
}

std::unique_ptr<IFolderModel> BackendRegistry::createFolderModel(QObject* parent) {
    return std::make_unique<QtFolderModel>(parent);
}

}  // namespace Oneg4FM
