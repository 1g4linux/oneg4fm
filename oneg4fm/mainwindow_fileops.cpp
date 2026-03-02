/*
 * Main window file operations implementation
 * oneg4fm/mainwindow_fileops.cpp
 */

#include "application.h"
#include "bulkrename.h"
#include "mainwindow.h"
#include "tabpage.h"

// New backend headers
#include "../src/backends/qt/qt_fileinfo.h"
#include "../src/core/backend_registry.h"
#include "../src/core/file_ops_contract.h"
#include "../src/ui/filepropertiesdialog.h"

// LibFM-Qt headers
#include "panel/panel.h"

// Qt headers
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QTimer>
#include <QtGlobal>
#include <unistd.h>
#include <memory>

namespace Oneg4FM {

namespace {

Settings& appSettings() {
    auto* app = qobject_cast<Application*>(qApp);
    Q_ASSERT(app);
    return app->settings();
}

bool shouldPreserveOwnershipForOps() {
    return appSettings().preservePermissions();
}

QStringList filePathListToStringList(const Panel::FilePathList& paths) {
    QStringList result;
    result.reserve(static_cast<int>(paths.size()));
    for (const auto& path : paths) {
        result.append(QString::fromUtf8(path.toString().get()));
    }
    return result;
}

std::shared_ptr<IFileInfo> convertToIFileInfo(const std::shared_ptr<const Panel::FileInfo>& fmInfo) {
    if (!fmInfo) {
        return {};
    }
    return std::make_shared<QtFileInfo>(QString::fromUtf8(fmInfo->path().toString().get()));
}

bool renameFileWithBackend(const std::shared_ptr<const Panel::FileInfo>& file, QWidget* parent) {
    if (!file) {
        return false;
    }

    bool ok = false;

    const QString currentPath = QString::fromUtf8(file->path().toString().get());
    const QFileInfo fileInfo(currentPath);
    const QString currentName = fileInfo.fileName();

    const QString newName =
        QInputDialog::getText(parent, QApplication::translate("MainWindow", "Rename"),
                              QApplication::translate("MainWindow", "New name:"), QLineEdit::Normal, currentName, &ok);

    if (ok && !newName.isEmpty() && newName != currentName) {
        const QString newPath = fileInfo.absolutePath() + QLatin1Char('/') + newName;

        const std::string srcNative = QFile::encodeName(currentPath).toStdString();
        const std::string dstNative = QFile::encodeName(newPath).toStdString();
        const std::string dstParentNative = QFile::encodeName(fileInfo.absolutePath()).toStdString();

        FileOpsContract::Request request;
        request.operation = FileOpsContract::Operation::Move;
        request.sources = {srcNative};
        request.destination.targetDir = dstParentNative;
        request.destination.mappingMode = FileOpsContract::DestinationMappingMode::ExplicitPerSource;
        request.destination.explicitTargets = {dstNative};
        request.conflictPolicy = FileOpsContract::ConflictPolicy::Overwrite;
        request.symlinkPolicy.followSymlinks = false;
        request.symlinkPolicy.copyMode = FileOpsContract::SymlinkCopyMode::CopyLinkAsLink;
        request.metadata.preserveOwnership = shouldPreserveOwnershipForOps();
        request.metadata.preservePermissions = true;
        request.metadata.preserveTimestamps = true;
        request.atomicity.requireAtomicReplace = false;
        request.atomicity.bestEffortAtomicMove = true;
        request.cancelGranularity = FileOpsContract::CancelCheckpointGranularity::PerChunk;
        request.linuxSafety.requireOpenat2Resolve = true;
        request.linuxSafety.requireLandlock = false;
        request.linuxSafety.requireSeccomp = false;
        request.linuxSafety.workerMode = FileOpsContract::WorkerMode::InProcess;
        request.routing.defaultBackend = FileOpsContract::Backend::Auto;
        request.routing.sourceKinds = {FileOpsContract::EndpointKind::NativePath};
        request.routing.destinationKind = FileOpsContract::EndpointKind::NativePath;
        request.routing.destinationBackend = FileOpsContract::Backend::Auto;

        const FileOpsContract::Result result = FileOpsContract::run(request);
        if (result.success) {
            return true;
        }

        const QString errorMsg = result.error.message.empty() ? QStringLiteral("Unknown error")
                                                              : QString::fromLocal8Bit(result.error.message.c_str());
        QMessageBox::critical(parent, QApplication::translate("MainWindow", "Error"),
                              QApplication::translate("MainWindow", "Failed to rename file: %1").arg(errorMsg));
        return false;
    }

    return ok;
}

}  // namespace

void MainWindow::on_actionFileProperties_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::FileProperties, *this);
}

void MainWindow::showFileProperties() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto files = page->selectedFiles();
    if (files.empty()) {
        return;
    }

    QList<std::shared_ptr<IFileInfo>> fileInfos;
    fileInfos.reserve(static_cast<int>(files.size()));
    for (const auto& file : files) {
        auto info = convertToIFileInfo(file);
        if (info) {
            fileInfos.append(std::move(info));
        }
    }

    if (fileInfos.isEmpty()) {
        return;
    }

    auto* dialog = new FilePropertiesDialog(fileInfos, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::on_actionFolderProperties_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::FolderProperties, *this);
}

void MainWindow::showFolderProperties() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto folder = page->folder();
    if (!folder) {
        return;
    }

    auto info = folder->info();
    if (!info) {
        return;
    }

    auto fileInfo = convertToIFileInfo(info);
    if (!fileInfo) {
        return;
    }

    auto* dialog = new FilePropertiesDialog(fileInfo, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::on_actionCopy_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::Copy, *this);
}

void MainWindow::copySelectionToClipboard() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    copyFilesToClipboard(paths);
}

void MainWindow::on_actionCut_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::Cut, *this);
}

void MainWindow::cutSelectionToClipboard() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    cutFilesToClipboard(paths);
}

void MainWindow::on_actionPaste_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::Paste, *this);
}

void MainWindow::pasteClipboardIntoCurrentFolder() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    pasteFilesFromClipboard(page->path());
}

void MainWindow::on_actionDelete_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::Delete, *this);
}

void MainWindow::deleteSelection() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    Settings& settings = appSettings();
    const auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    const bool useTrash = false;
    const int itemCount = static_cast<int>(paths.size());

    // Respect confirmation preferences for trash or delete
    const bool needsConfirm = useTrash ? settings.confirmTrash() : settings.confirmDelete();
    if (needsConfirm) {
        const QString countText =
            (itemCount == 1) ? QString::fromUtf8(paths.front().toString().get()) : tr("%n items", nullptr, itemCount);
        const QString title = useTrash ? tr("Move to Trash") : tr("Delete Files");
        const QString question = useTrash ? tr("Move %1 to trash?").arg(countText)
                                          : tr("Permanently delete %1? This cannot be undone.").arg(countText);

        const auto reply =
            QMessageBox::warning(this, title, question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    auto fileOps = BackendRegistry::createFileOps();
    if (!fileOps) {
        QMessageBox::warning(this, tr("Delete Failed"), tr("File operations backend is not available."));
        return;
    }

    // Keep the async backend alive until it finishes.
    IFileOps* fileOpsPtr = fileOps.release();
    fileOpsPtr->setParent(this);

    auto* progressDialog = new QProgressDialog(tr("Deleting files..."), tr("Cancel"), 0, 0, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setMinimumDuration(0);
    progressDialog->setValue(0);
    progressDialog->setLabelText(tr("Deleting files..."));

    const auto cancelledByUser = std::make_shared<bool>(false);
    connect(progressDialog, &QProgressDialog::canceled, this, [fileOpsPtr, cancelledByUser] {
        *cancelledByUser = true;
        fileOpsPtr->cancel();
    });
    connect(fileOpsPtr, &IFileOps::progress, progressDialog, [progressDialog](const FileOpProgress& info) {
        if (info.filesTotal > 0) {
            if (progressDialog->maximum() != info.filesTotal) {
                progressDialog->setMaximum(info.filesTotal);
            }
            progressDialog->setValue(qMin(info.filesDone, info.filesTotal));
        }
        else {
            progressDialog->setMaximum(0);  // Busy indicator when total is unknown.
        }
        if (!info.currentPath.isEmpty()) {
            progressDialog->setLabelText(tr("Deleting %1").arg(info.currentPath));
        }
    });
    connect(fileOpsPtr, &IFileOps::finished, this,
            [this, progressDialog, cancelledByUser](bool success, const QString& errorMessage) {
                progressDialog->hide();
                progressDialog->deleteLater();
                if (!success && !*cancelledByUser) {
                    QMessageBox::warning(
                        this, tr("Delete Failed"),
                        errorMessage.isEmpty() ? tr("The selected files could not be deleted.") : errorMessage);
                }
            });
    connect(fileOpsPtr, &IFileOps::finished, fileOpsPtr, &QObject::deleteLater);

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = filePathListToStringList(paths);
    req.destination.clear();
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.preserveOwnership = shouldPreserveOwnershipForOps();

    progressDialog->show();
    progressDialog->raise();
    progressDialog->activateWindow();
    QTimer::singleShot(0, fileOpsPtr, [fileOpsPtr, req]() { fileOpsPtr->start(req); });
}

void MainWindow::on_actionRename_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::Rename, *this);
}

void MainWindow::renameSelection() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto files = page->selectedFiles();

    if (files.size() == 1) {
        auto* folderView = page->folderView();
        if (!folderView) {
            return;
        }

        QAbstractItemView* view = folderView->childView();
        if (!view || !view->selectionModel()) {
            return;
        }

        const QModelIndexList selIndexes = view->selectionModel()->selectedIndexes();
        if (selIndexes.isEmpty()) {
            return;
        }

        const QModelIndex editIndex = selIndexes.first();
        view->setCurrentIndex(editIndex);
        view->scrollTo(editIndex);
        view->edit(editIndex);
        return;
    }

    if (!files.empty()) {
        for (auto& file : files) {
            if (!renameFileWithBackend(file, this)) {
                break;
            }
        }
    }
}

void MainWindow::on_actionBulkRename_triggered() {
    MainWindowFileOpsCommands::execute(MainWindowFileOpsCommands::Id::BulkRename, *this);
}

void MainWindow::bulkRenameSelection() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto files = page->selectedFiles();
    if (files.empty()) {
        return;
    }

    BulkRenamer(files, this);
}

void MainWindow::on_actionSelectAll_triggered() {
    MainWindowSelectionCommands::execute(MainWindowSelectionCommands::Id::SelectAll, *this);
}

void MainWindow::on_actionDeselectAll_triggered() {
    MainWindowSelectionCommands::execute(MainWindowSelectionCommands::Id::DeselectAll, *this);
}

void MainWindow::on_actionInvertSelection_triggered() {
    MainWindowSelectionCommands::execute(MainWindowSelectionCommands::Id::InvertSelection, *this);
}

bool MainWindow::hasSingleSelectedPath() const {
    TabPage* page = currentPage();
    return page && page->selectedFilePaths().size() == 1;
}

bool MainWindow::hasSelectedFiles() const {
    TabPage* page = currentPage();
    return page && !page->selectedFiles().empty();
}

bool MainWindow::hasAccessibleSelection() const {
    TabPage* page = currentPage();
    if (!page) {
        return false;
    }

    const auto files = page->selectedFiles();
    for (const auto& file : files) {
        if (file && file->isAccessible()) {
            return true;
        }
    }
    return false;
}

bool MainWindow::hasDeletableSelection() const {
    TabPage* page = currentPage();
    if (!page) {
        return false;
    }

    const auto files = page->selectedFiles();
    for (const auto& file : files) {
        if (file && file->isDeletable()) {
            return true;
        }
    }
    return false;
}

bool MainWindow::canPasteIntoCurrentFolder() const {
    TabPage* page = currentPage();
    if (!page || !page->folder()) {
        return false;
    }

    const auto info = page->folder()->info();
    return info && info->isWritable();
}

int MainWindow::renamableSelectionCount() const {
    TabPage* page = currentPage();
    if (!page) {
        return 0;
    }

    int renamable = 0;
    const auto files = page->selectedFiles();
    for (const auto& file : files) {
        if (file && file->canSetName()) {
            ++renamable;
        }
    }
    return renamable;
}

void MainWindow::selectAllFiles() {
    if (TabPage* page = currentPage()) {
        page->selectAll();
    }
}

void MainWindow::deselectAllFiles() {
    if (TabPage* page = currentPage()) {
        page->deselectAll();
    }
}

void MainWindow::invertFileSelection() {
    if (TabPage* page = currentPage()) {
        page->invertSelection();
    }
}

void MainWindow::copySelectedPathToClipboard() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto paths = page->selectedFilePaths();
    if (paths.size() != 1) {
        return;
    }

    QApplication::clipboard()->setText(QString::fromUtf8(paths.front().toString().get()));
}

void MainWindow::on_actionCopyFullPath_triggered() {
    MainWindowSelectionCommands::execute(MainWindowSelectionCommands::Id::CopyFullPath, *this);
}

void MainWindow::on_actionCleanPerFolderConfig_triggered() {
    const auto r = QMessageBox::question(
        this, tr("Cleaning Folder Settings"),
        tr("Do you want to remove settings of nonexistent folders?\nThey might be useful if those folders are "
           "created again."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (r == QMessageBox::Yes) {
        auto* app = qobject_cast<Application*>(qApp);
        if (app) {
            app->cleanPerFolderConfig();
        }
    }
}

void MainWindow::openFolderAndSelectFiles(const Panel::FilePathList& files, bool inNewTab) {
    if (files.empty()) {
        return;
    }

    auto path = files.front().parent();
    if (!path) {
        return;
    }

    if (!inNewTab) {
        auto* win = new MainWindow(path);
        win->show();
        if (auto* page = win->currentPage()) {
            page->setFilesToSelect(files);
        }
    }
    else {
        auto* newPage = new TabPage(this);
        addTabWithPage(newPage, activeViewFrame_, std::move(path));
        newPage->setFilesToSelect(files);
    }
}

}  // namespace Oneg4FM
