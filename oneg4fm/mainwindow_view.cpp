/*
 * Main window view management implementation
 * oneg4fm/mainwindow_view.cpp
 */

#include "application.h"
#include "mainwindow.h"
#include "mainwindow_view_controller.h"
#include "tabpage.h"

// Qt Headers
#include <QApplication>
#include <QSplitter>
#include <QStackedWidget>

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

//-----------------------------------------------------------------------------
// Traversal Helpers
//-----------------------------------------------------------------------------

// Helper to iterate over every TabPage in the CURRENT window only
template <typename Func>
void MainWindow::forEachTabPageLocal(Func func) {
    if (!ui.viewSplitter)
        return;

    for (int i = 0; i < ui.viewSplitter->count(); ++i) {
        if (auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i))) {
            auto* stack = viewFrame->getStackedWidget();
            for (int j = 0; j < stack->count(); ++j) {
                if (auto* page = qobject_cast<TabPage*>(stack->widget(j))) {
                    func(page);
                }
            }
        }
    }
}

// Helper to iterate over every TabPage in ALL open MainWindow instances
// This is necessary for Global settings (like "Show Thumbnails") that should update everywhere immediately.
template <typename Func>
void MainWindow::forEachTabPageGlobal(Func func) {
    const auto windows = qApp->topLevelWidgets();
    for (auto* widget : windows) {
        if (auto* mw = qobject_cast<MainWindow*>(widget)) {
            // Process tabs in this window
            if (mw->ui.viewSplitter) {
                for (int i = 0; i < mw->ui.viewSplitter->count(); ++i) {
                    if (auto* viewFrame = qobject_cast<ViewFrame*>(mw->ui.viewSplitter->widget(i))) {
                        auto* stack = viewFrame->getStackedWidget();
                        for (int j = 0; j < stack->count(); ++j) {
                            if (auto* page = qobject_cast<TabPage*>(stack->widget(j))) {
                                func(mw, page);
                            }
                        }
                    }
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// View Mode Actions
//-----------------------------------------------------------------------------

void MainWindow::on_actionIconView_triggered() {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::IconView, *this);
}

void MainWindow::on_actionCompactView_triggered() {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::CompactView, *this);
}

void MainWindow::on_actionDetailedList_triggered() {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::DetailedView, *this);
}

void MainWindow::on_actionThumbnailView_triggered() {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::ThumbnailView, *this);
}

void MainWindow::setIconMode() {
    MainWindowViewController::setViewMode(*this, MainWindowViewController::ViewMode::Icon);
}

void MainWindow::setCompactMode() {
    MainWindowViewController::setViewMode(*this, MainWindowViewController::ViewMode::Compact);
}

void MainWindow::setDetailedMode() {
    MainWindowViewController::setViewMode(*this, MainWindowViewController::ViewMode::Detailed);
}

void MainWindow::setThumbnailMode() {
    MainWindowViewController::setViewMode(*this, MainWindowViewController::ViewMode::Thumbnail);
}

//-----------------------------------------------------------------------------
// Visibility & Filter Actions
//-----------------------------------------------------------------------------

void MainWindow::on_actionShowHidden_triggered(bool checked) {
    appSettings().setShowHidden(checked);
    forEachTabPageGlobal([checked](MainWindow* mw, TabPage* page) {
        mw->ui.actionShowHidden->setChecked(checked);
        page->setShowHidden(checked);
        if (mw->currentPage() == page && mw->ui.sidePane) {
            mw->ui.sidePane->setShowHidden(checked);
        }
    });
}

void MainWindow::on_actionShowThumbnails_triggered(bool checked) {
    // This is a global setting, so update all pages in all windows
    forEachTabPageGlobal([checked](MainWindow* mw, TabPage* page) {
        // Update the menu action state in other windows so they stay in sync
        mw->ui.actionShowThumbnails->setChecked(checked);
        page->setShowThumbnails(checked);
    });
}

void MainWindow::on_actionFilter_triggered(bool checked) {
    MainWindowViewController::setFilterBarsPersistent(*this, checked);
}

void MainWindow::on_actionUnfilter_triggered() {
    MainWindowViewController::clearFilters(*this);
}

void MainWindow::on_actionShowFilter_triggered() {
    MainWindowViewController::showFilterBar(*this);
}

void MainWindow::on_actionPreserveView_triggered(bool checked) {
    if (auto* page = currentPage()) {
        page->setCustomizedView(checked);
        if (checked) {
            ui.actionPreserveViewRecursive->setChecked(false);
        }
        ui.actionGoToCustomizedViewSource->setVisible(page->hasInheritedCustomizedView());
        setTabIcon(page);
    }
}

void MainWindow::on_actionPreserveViewRecursive_triggered(bool checked) {
    if (auto* page = currentPage()) {
        page->setCustomizedView(checked, true);
        if (checked) {
            ui.actionPreserveView->setChecked(false);
        }
        ui.actionGoToCustomizedViewSource->setVisible(page->hasInheritedCustomizedView());
        setTabIcon(page);
    }
}

void MainWindow::on_actionGoToCustomizedViewSource_triggered() {
    if (auto* page = currentPage()) {
        page->goToCustomizedViewSource();
        updateUIForCurrentPage();
    }
}

//-----------------------------------------------------------------------------
// Sorting Actions
//-----------------------------------------------------------------------------

void MainWindow::on_actionByFileName_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByFileName, *this);
}

void MainWindow::on_actionByMTime_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByMTime, *this);
}

void MainWindow::on_actionByCrTime_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByCrTime, *this);
}

void MainWindow::on_actionByDTime_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByDTime, *this);
}

void MainWindow::on_actionByOwner_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByOwner, *this);
}

void MainWindow::on_actionByGroup_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByGroup, *this);
}

void MainWindow::on_actionByFileSize_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByFileSize, *this);
}

void MainWindow::on_actionByFileType_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortByFileType, *this);
}

void MainWindow::sortByFileName() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::FileName);
}

void MainWindow::sortByMTime() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::MTime);
}

void MainWindow::sortByCrTime() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::CrTime);
}

void MainWindow::sortByDTime() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::DTime);
}

void MainWindow::sortByOwner() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::Owner);
}

void MainWindow::sortByGroup() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::Group);
}

void MainWindow::sortByFileSize() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::FileSize);
}

void MainWindow::sortByFileType() {
    MainWindowViewController::sortByColumn(*this, MainWindowViewController::SortColumn::FileType);
}

void MainWindow::on_actionAscending_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortAscending, *this);
}

void MainWindow::on_actionDescending_triggered(bool /*checked*/) {
    MainWindowViewCommands::execute(MainWindowViewCommands::Id::SortDescending, *this);
}

void MainWindow::sortAscending() {
    MainWindowViewController::sortByOrder(*this, Qt::AscendingOrder);
}

void MainWindow::sortDescending() {
    MainWindowViewController::sortByOrder(*this, Qt::DescendingOrder);
}

void MainWindow::on_actionCaseSensitive_triggered(bool checked) {
    MainWindowViewController::setSortCaseSensitive(*this, checked);
}

void MainWindow::on_actionFolderFirst_triggered(bool checked) {
    MainWindowViewController::setSortFolderFirst(*this, checked);
}

ViewFrame* MainWindow::activeViewFrame() const {
    return activeViewFrame_;
}

void MainWindow::applyViewMode(MainWindowViewController::ViewMode mode) {
    if (auto* page = currentPage()) {
        Panel::FolderView::ViewMode viewMode = Panel::FolderView::DetailedListMode;

        switch (mode) {
            case MainWindowViewController::ViewMode::Icon:
                viewMode = Panel::FolderView::IconMode;
                break;
            case MainWindowViewController::ViewMode::Compact:
                viewMode = Panel::FolderView::CompactMode;
                break;
            case MainWindowViewController::ViewMode::Detailed:
                viewMode = Panel::FolderView::DetailedListMode;
                break;
            case MainWindowViewController::ViewMode::Thumbnail:
                viewMode = Panel::FolderView::ThumbnailMode;
                break;
        }

        page->setViewMode(viewMode);
        setTabIcon(page);
    }
}

void MainWindow::applySort(MainWindowViewController::SortColumn column, Qt::SortOrder order) {
    if (auto* page = currentPage()) {
        Panel::FolderModel::ColumnId columnId = Panel::FolderModel::ColumnFileName;
        switch (column) {
            case MainWindowViewController::SortColumn::FileName:
                columnId = Panel::FolderModel::ColumnFileName;
                break;
            case MainWindowViewController::SortColumn::MTime:
                columnId = Panel::FolderModel::ColumnFileMTime;
                break;
            case MainWindowViewController::SortColumn::CrTime:
                columnId = Panel::FolderModel::ColumnFileCrTime;
                break;
            case MainWindowViewController::SortColumn::DTime:
                columnId = Panel::FolderModel::ColumnFileDTime;
                break;
            case MainWindowViewController::SortColumn::Owner:
                columnId = Panel::FolderModel::ColumnFileOwner;
                break;
            case MainWindowViewController::SortColumn::Group:
                columnId = Panel::FolderModel::ColumnFileGroup;
                break;
            case MainWindowViewController::SortColumn::FileSize:
                columnId = Panel::FolderModel::ColumnFileSize;
                break;
            case MainWindowViewController::SortColumn::FileType:
                columnId = Panel::FolderModel::ColumnFileType;
                break;
        }
        page->sort(columnId, order);
    }
}

Qt::SortOrder MainWindow::currentSortOrder() const {
    if (auto* page = currentPage()) {
        return page->sortOrder();
    }
    return Qt::AscendingOrder;
}

MainWindowViewController::SortColumn MainWindow::currentSortColumn() const {
    if (auto* page = currentPage()) {
        switch (page->sortColumn()) {
            case Panel::FolderModel::ColumnFileName:
                return MainWindowViewController::SortColumn::FileName;
            case Panel::FolderModel::ColumnFileMTime:
                return MainWindowViewController::SortColumn::MTime;
            case Panel::FolderModel::ColumnFileCrTime:
                return MainWindowViewController::SortColumn::CrTime;
            case Panel::FolderModel::ColumnFileDTime:
                return MainWindowViewController::SortColumn::DTime;
            case Panel::FolderModel::ColumnFileOwner:
                return MainWindowViewController::SortColumn::Owner;
            case Panel::FolderModel::ColumnFileGroup:
                return MainWindowViewController::SortColumn::Group;
            case Panel::FolderModel::ColumnFileSize:
                return MainWindowViewController::SortColumn::FileSize;
            case Panel::FolderModel::ColumnFileType:
                return MainWindowViewController::SortColumn::FileType;
            default:
                break;
        }
    }

    return MainWindowViewController::SortColumn::FileName;
}

void MainWindow::applySortCaseSensitive(bool enabled) {
    if (auto* page = currentPage()) {
        page->setSortCaseSensitive(enabled);
    }
}

void MainWindow::applySortFolderFirst(bool enabled) {
    if (auto* page = currentPage()) {
        page->setSortFolderFirst(enabled);
    }
}

void MainWindow::setFilterBarsPersistent(bool enabled) {
    appSettings().setShowFilter(enabled);
    forEachTabPageGlobal([enabled](MainWindow* mw, TabPage* page) {
        mw->ui.actionFilter->setChecked(enabled);
        page->transientFilterBar(!enabled);
    });
}

void MainWindow::clearFiltersInCurrentWindow() {
    forEachTabPageLocal([](TabPage* page) { page->clearFilter(); });
}

void MainWindow::showCurrentFilterBar() {
    if (auto* page = currentPage()) {
        page->showFilterBar();
    }
}

void MainWindow::syncPathBarForFrame(ViewFrame* frame) {
    if (!frame) {
        return;
    }

    TabPage* page = currentPage(frame);
    if (!page) {
        return;
    }

    if (auto* pathBar = qobject_cast<Panel::PathBar*>(frame->getTopBar())) {
        pathBar->setPath(page->path());
        return;
    }

    if (auto* pathEntry = qobject_cast<Panel::PathEdit*>(frame->getTopBar())) {
        pathEntry->setText(page->pathName());
        return;
    }

    if (frame == activeViewFrame_ && !splitView_) {
        if (pathBar_) {
            pathBar_->setPath(page->path());
        }
        else if (pathEntry_) {
            pathEntry_->setText(page->pathName());
        }
    }
}

void MainWindow::projectCurrentPageUi(bool setFocus) {
    updateUIForCurrentPage(setFocus);
}

}  // namespace Oneg4FM
