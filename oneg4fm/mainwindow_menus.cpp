/*
 * Main window menu management implementation
 * oneg4fm/mainwindow_menus.cpp
 */

#include <QActionGroup>
#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QToolBar>
#include <QToolButton>

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

// If you are migrating away from libfm, replace these with your new backend interfaces
#include "panel/panel.h"

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

void MainWindow::toggleMenuBar(bool /*checked*/) {
    Settings& settings = appSettings();
    const bool showMenuBar = !settings.showMenuBar();

    if (!showMenuBar) {
        const auto reply = QMessageBox::warning(
            this, tr("Hide menu bar"), tr("This will hide the menu bar completely, use Ctrl+M to show it again."),
            QMessageBox::Ok | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) {
            ui.actionMenu_bar->setChecked(true);
            return;
        }
    }

    ui.menubar->setVisible(showMenuBar);
    ui.actionMenu_bar->setChecked(showMenuBar);

    if (menuSep_) {
        menuSep_->setVisible(!showMenuBar);
    }

    ui.actionMenu->setVisible(!showMenuBar);
    settings.setShowMenuBar(showMenuBar);
}

void MainWindow::on_actionMenu_triggered() {
    if (!ui.menubar) {
        return;
    }

    QMenu popup(this);
    const auto actions = ui.menubar->actions();
    for (auto* action : actions) {
        if (!action || !action->isVisible()) {
            continue;
        }
        popup.addAction(action);
    }

    QPoint pos = QCursor::pos();
    if (ui.toolBar) {
        if (QWidget* toolButton = ui.toolBar->widgetForAction(ui.actionMenu)) {
            pos = toolButton->mapToGlobal(QPoint(0, toolButton->height()));
        }
    }

    popup.exec(pos);
}

void MainWindow::updateStatusBarForCurrentPage() {
    TabPage* tabPage = currentPage();
    if (!tabPage) {
        ui.statusbar->clearMessage();
        return;
    }

    QString text = tabPage->statusText(TabPage::StatusTextSelectedFiles);
    if (text.isEmpty()) {
        text = tabPage->statusText(TabPage::StatusTextNormal);
    }
    ui.statusbar->showMessage(text);
}

void MainWindow::updateViewMenuForCurrentPage() {
    if (updatingViewMenu_) {  // prevent recursive calls
        return;
    }

    updatingViewMenu_ = true;

    TabPage* tabPage = currentPage();
    if (tabPage) {
        // update menus
        ui.actionShowHidden->setChecked(tabPage->showHidden());
        ui.actionPreserveView->setChecked(tabPage->hasCustomizedView() && !tabPage->hasRecursiveCustomizedView());
        ui.actionPreserveViewRecursive->setChecked(tabPage->hasRecursiveCustomizedView());
        ui.actionGoToCustomizedViewSource->setVisible(tabPage->hasInheritedCustomizedView());

        // view mode
        QAction* modeAction = nullptr;

        switch (tabPage->viewMode()) {
            case Panel::FolderView::IconMode:
                modeAction = ui.actionIconView;
                break;
            case Panel::FolderView::CompactMode:
                modeAction = ui.actionCompactView;
                break;
            case Panel::FolderView::DetailedListMode:
                modeAction = ui.actionDetailedList;
                break;
            case Panel::FolderView::ThumbnailMode:
                modeAction = ui.actionThumbnailView;
                break;
        }

        if (modeAction) {
            modeAction->setChecked(true);
        }

        // sort menu
        // WARNING: Since libfm-qt may have a column that is not handled here,
        // we should prevent a crash by checking bounds carefully.

        QAction* sortActions[Panel::FolderModel::NumOfColumns];
        std::fill(std::begin(sortActions), std::end(sortActions), nullptr);

        sortActions[Panel::FolderModel::ColumnFileName] = ui.actionByFileName;
        sortActions[Panel::FolderModel::ColumnFileMTime] = ui.actionByMTime;
        sortActions[Panel::FolderModel::ColumnFileCrTime] = ui.actionByCrTime;
        sortActions[Panel::FolderModel::ColumnFileDTime] = ui.actionByDTime;
        sortActions[Panel::FolderModel::ColumnFileSize] = ui.actionByFileSize;
        sortActions[Panel::FolderModel::ColumnFileType] = ui.actionByFileType;
        sortActions[Panel::FolderModel::ColumnFileOwner] = ui.actionByOwner;
        sortActions[Panel::FolderModel::ColumnFileGroup] = ui.actionByGroup;

        // Ensure we handle action groups correctly
        if (auto* group = ui.actionByFileName->actionGroup()) {
            const auto groupActions = group->actions();

            // Validate index before array access
            const int sortCol = tabPage->sortColumn();
            QAction* targetAction = nullptr;

            if (sortCol >= 0 && sortCol < Panel::FolderModel::NumOfColumns) {
                targetAction = sortActions[sortCol];
            }

            if (targetAction && groupActions.contains(targetAction)) {
                targetAction->setChecked(true);
            }
            else {
                for (auto* a : groupActions) {
                    a->setChecked(false);
                }
            }
        }

        if (auto path = tabPage->path()) {
            ui.actionByDTime->setVisible(strcmp(path.toString().get(), "trash:///") == 0);
        }

        if (tabPage->sortOrder() == Qt::AscendingOrder) {
            ui.actionAscending->setChecked(true);
        }
        else {
            ui.actionDescending->setChecked(true);
        }

        ui.actionCaseSensitive->setChecked(tabPage->sortCaseSensitive());
        ui.actionFolderFirst->setChecked(tabPage->sortFolderFirst());
    }

    updatingViewMenu_ = false;
}

// Update the enabled state of File and Edit actions for selected files
void MainWindow::updateSelectedActions() {
    ui.actionFileProperties->setEnabled(
        MainWindowFileOpsCommands::canExecute(MainWindowFileOpsCommands::Id::FileProperties, *this));
    ui.actionFolderProperties->setEnabled(
        MainWindowFileOpsCommands::canExecute(MainWindowFileOpsCommands::Id::FolderProperties, *this));
    ui.actionCopy->setEnabled(MainWindowSelectionCommands::canExecute(MainWindowSelectionCommands::Id::Copy, *this));
    ui.actionCut->setEnabled(MainWindowSelectionCommands::canExecute(MainWindowSelectionCommands::Id::Cut, *this));
    ui.actionDelete->setEnabled(MainWindowFileOpsCommands::canExecute(MainWindowFileOpsCommands::Id::Delete, *this));
    ui.actionRename->setEnabled(MainWindowFileOpsCommands::canExecute(MainWindowFileOpsCommands::Id::Rename, *this));
    ui.actionBulkRename->setEnabled(
        MainWindowFileOpsCommands::canExecute(MainWindowFileOpsCommands::Id::BulkRename, *this));
    ui.actionPaste->setEnabled(MainWindowSelectionCommands::canExecute(MainWindowSelectionCommands::Id::Paste, *this));
    ui.actionCopyFullPath->setEnabled(
        MainWindowSelectionCommands::canExecute(MainWindowSelectionCommands::Id::CopyFullPath, *this));

    const bool canModifySelection =
        MainWindowSelectionCommands::canExecute(MainWindowSelectionCommands::Id::SelectAll, *this);
    ui.actionSelectAll->setEnabled(canModifySelection);
    ui.actionDeselectAll->setEnabled(canModifySelection);
    ui.actionInvertSelection->setEnabled(canModifySelection);
}

void MainWindow::updateUIForCurrentPage(bool setFocus) {
    TabPage* tabPage = currentPage();

    if (tabPage) {
        setWindowTitle(tabPage->title());

        syncPathBarForFrame(activeViewFrame_);

        ui.statusbar->showMessage(tabPage->statusText());

        if (setFocus && tabPage->folderView() && tabPage->folderView()->childView()) {
            tabPage->folderView()->childView()->setFocus();
        }

        // update side pane
        if (ui.sidePane) {
            Panel::FilePath currentPath = tabPage->path();
            if (currentPath) {
                ui.sidePane->setCurrentPath(currentPath);
            }
            ui.sidePane->setShowHidden(tabPage->showHidden());
        }

        // update back/forward/up toolbar buttons
        ui.actionGoUp->setEnabled(tabPage->canUp());
        ui.actionGoBack->setEnabled(tabPage->canBackward());
        ui.actionGoForward->setEnabled(tabPage->canForward());

        updateViewMenuForCurrentPage();
        updateStatusBarForCurrentPage();
    }

    // also update the enabled state of File and Edit actions
    updateSelectedActions();

    bool isWritable = false;
    bool isNative = false;

    if (tabPage && tabPage->folder()) {
        if (auto info = tabPage->folder()->info()) {
            isWritable = info->isWritable();
            isNative = info->isNative();
        }
    }

    ui.actionPaste->setEnabled(isWritable);
    ui.menuCreateNew->setEnabled(isWritable);
    // disable creation shortcuts too
    ui.actionNewFolder->setEnabled(isWritable);
    ui.actionNewBlankFile->setEnabled(isWritable);
    ui.actionCreateLauncher->setEnabled(isWritable && isNative);
}

}  // namespace Oneg4FM
