/*
 * Main window view management implementation
 * oneg4fm/mainwindow_view.cpp
 */

#include "application.h"
#include "mainwindow.h"
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
    if (auto* page = currentPage()) {
        page->setViewMode(Panel::FolderView::IconMode);
        setTabIcon(page);
    }
}

void MainWindow::on_actionCompactView_triggered() {
    if (auto* page = currentPage()) {
        page->setViewMode(Panel::FolderView::CompactMode);
        setTabIcon(page);
    }
}

void MainWindow::on_actionDetailedList_triggered() {
    if (auto* page = currentPage()) {
        page->setViewMode(Panel::FolderView::DetailedListMode);
        setTabIcon(page);
    }
}

void MainWindow::on_actionThumbnailView_triggered() {
    if (auto* page = currentPage()) {
        page->setViewMode(Panel::FolderView::ThumbnailMode);
        setTabIcon(page);
    }
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
    appSettings().setShowFilter(checked);

    // Show/hide filter-bars and disable/enable their transience for all tabs
    // in all windows because this is a global setting.
    forEachTabPageGlobal([checked](MainWindow* mw, TabPage* page) {
        mw->ui.actionFilter->setChecked(checked);
        page->transientFilterBar(!checked);
    });
}

void MainWindow::on_actionUnfilter_triggered() {
    // Clear filters for all tabs in the CURRENT window only
    forEachTabPageLocal([](TabPage* page) { page->clearFilter(); });
}

void MainWindow::on_actionShowFilter_triggered() {
    if (auto* page = currentPage()) {
        page->showFilterBar();
    }
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
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileName, page->sortOrder());
    }
}

void MainWindow::on_actionByMTime_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileMTime, page->sortOrder());
    }
}

void MainWindow::on_actionByCrTime_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileCrTime, page->sortOrder());
    }
}

void MainWindow::on_actionByDTime_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileDTime, page->sortOrder());
    }
}

void MainWindow::on_actionByOwner_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileOwner, page->sortOrder());
    }
}

void MainWindow::on_actionByGroup_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileGroup, page->sortOrder());
    }
}

void MainWindow::on_actionByFileSize_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileSize, page->sortOrder());
    }
}

void MainWindow::on_actionByFileType_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(Panel::FolderModel::ColumnFileType, page->sortOrder());
    }
}

void MainWindow::on_actionAscending_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(page->sortColumn(), Qt::AscendingOrder);
    }
}

void MainWindow::on_actionDescending_triggered(bool /*checked*/) {
    if (auto* page = currentPage()) {
        page->sort(page->sortColumn(), Qt::DescendingOrder);
    }
}

void MainWindow::on_actionCaseSensitive_triggered(bool checked) {
    if (auto* page = currentPage()) {
        page->setSortCaseSensitive(checked);
    }
}

void MainWindow::on_actionFolderFirst_triggered(bool checked) {
    if (auto* page = currentPage()) {
        page->setSortFolderFirst(checked);
    }
}

}  // namespace Oneg4FM
