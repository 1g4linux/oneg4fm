/*
 * Main window path bar implementation
 * oneg4fm/mainwindow_pathbar.cpp
 */

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

// Qt Headers
#include <QSizePolicy>
#include <QToolBar>  // Ensure toolBar access

// LibFM-Qt Headers
#include "panel/panel.h"

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

void MainWindow::createPathBar(bool usePathButtons) {
    // path bars/entries may be created after tab pages so their paths/texts should be set
    if (splitView_) {
        createSplitViewPathBar(usePathButtons);
    }
    else {
        createSingleViewPathBar(usePathButtons);
    }
}

void MainWindow::createSplitViewPathBar(bool usePathButtons) {
    if (!ui.viewSplitter)
        return;

    for (int i = 0; i < ui.viewSplitter->count(); ++i) {
        if (auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i))) {
            viewFrame->createTopBar(usePathButtons);
            TabPage* curPage = currentPage(viewFrame);

            if (auto* pathBar = qobject_cast<Panel::PathBar*>(viewFrame->getTopBar())) {
                connect(pathBar, &Panel::PathBar::chdir, this, &MainWindow::onPathBarChdir);
                connect(pathBar, &Panel::PathBar::middleClickChdir, this, &MainWindow::onPathBarMiddleClickChdir);
                connect(pathBar, &Panel::PathBar::editingFinished, this, &MainWindow::onResetFocus);
                if (curPage) {
                    pathBar->setPath(curPage->path());
                }
            }
            else if (auto* pathEntry = qobject_cast<Panel::PathEdit*>(viewFrame->getTopBar())) {
                connect(pathEntry, &Panel::PathEdit::returnPressed, this, &MainWindow::onPathEntryReturnPressed);
                if (curPage) {
                    pathEntry->setText(curPage->pathName());
                }
            }
        }
    }
}

void MainWindow::createSingleViewPathBar(bool usePathButtons) {
    QWidget* bar = nullptr;
    TabPage* curPage = currentPage();

    if (usePathButtons) {
        if (pathEntry_) {
            delete pathEntry_;
            pathEntry_ = nullptr;
        }
        if (!pathBar_) {
            pathBar_ = new Panel::PathBar(this);
            bar = pathBar_;

            connect(pathBar_, &Panel::PathBar::chdir, this, &MainWindow::onPathBarChdir);
            connect(pathBar_, &Panel::PathBar::middleClickChdir, this, &MainWindow::onPathBarMiddleClickChdir);
            connect(pathBar_, &Panel::PathBar::editingFinished, this, &MainWindow::onResetFocus);

            if (curPage) {
                pathBar_->setPath(curPage->path());
            }
        }
    }
    else {
        if (pathBar_) {
            delete pathBar_;
            pathBar_ = nullptr;
        }
        if (!pathEntry_) {
            pathEntry_ = new Panel::PathEdit(this);
            bar = pathEntry_;

            connect(pathEntry_, &Panel::PathEdit::returnPressed, this, &MainWindow::onPathEntryReturnPressed);

            if (curPage) {
                pathEntry_->setText(curPage->pathName());
            }
        }
    }

    if (bar && ui.toolBar) {
        bar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        bar->setMinimumWidth(260);
        bar->setMaximumWidth(520);

        ui.toolBar->insertWidget(ui.actionGo, bar);
        ui.actionGo->setVisible(!usePathButtons);
    }
}

void MainWindow::onPathEntryReturnPressed() {
    Panel::PathEdit* pathEntry = pathEntry_;

    // In split view, the sender is the specific path entry
    if (!pathEntry) {
        pathEntry = qobject_cast<Panel::PathEdit*>(sender());
    }

    if (!pathEntry) {
        return;
    }

    const QString text = pathEntry->text();
    // Using fromPathStr handles local vs URI logic internally in libfm
    chdir(Panel::FilePath::fromPathStr(text.toLocal8Bit().constData()));
}

void MainWindow::onPathBarChdir(const Panel::FilePath& dirPath) {
    TabPage* page = nullptr;
    ViewFrame* viewFrame = nullptr;

    if (pathBar_) {
        // Single view mode
        page = currentPage();
        viewFrame = activeViewFrame_;
    }
    else {
        // Split view mode: find which PathBar sent the signal
        auto* pathBar = qobject_cast<Panel::PathBar*>(sender());
        if (pathBar) {
            viewFrame = qobject_cast<ViewFrame*>(pathBar->parentWidget());
            if (viewFrame) {
                page = currentPage(viewFrame);
            }
        }
    }

    if (page && viewFrame && dirPath != page->path()) {
        chdir(dirPath, viewFrame);
    }
}

void MainWindow::onPathBarMiddleClickChdir(const Panel::FilePath& dirPath) {
    ViewFrame* viewFrame = nullptr;

    if (pathBar_) {
        viewFrame = activeViewFrame_;
    }
    else {
        auto* pathBar = qobject_cast<Panel::PathBar*>(sender());
        if (pathBar) {
            viewFrame = qobject_cast<ViewFrame*>(pathBar->parentWidget());
        }
    }

    if (viewFrame) {
        addTab(dirPath, viewFrame);
    }
}

void MainWindow::on_actionLocationBar_triggered(bool checked) {
    if (!checked) {
        return;
    }

    // switch to text entry mode
    createPathBar(false);
    appSettings().setPathBarButtons(false);
}

void MainWindow::on_actionPathButtons_triggered(bool checked) {
    if (!checked) {
        return;
    }

    // switch to buttons mode
    createPathBar(true);
    appSettings().setPathBarButtons(true);
}

void MainWindow::on_actionGo_triggered() {
    onPathEntryReturnPressed();
}

void MainWindow::onResetFocus() {
    if (TabPage* page = currentPage()) {
        if (page->folderView() && page->folderView()->childView()) {
            page->folderView()->childView()->setFocus();
        }
    }
}

void MainWindow::focusPathEntry() {
    // Focus the path bar/entry.
    // In split view, focus the active frame's bar.
    if (splitView_) {
        if (!activeViewFrame_) {
            return;
        }

        if (auto* pathBar = qobject_cast<Panel::PathBar*>(activeViewFrame_->getTopBar())) {
            pathBar->openEditor();
        }
        else if (auto* pathEntry = qobject_cast<Panel::PathEdit*>(activeViewFrame_->getTopBar())) {
            pathEntry->setFocus();
            pathEntry->selectAll();
        }
    }
    else {
        // Single view logic
        if (pathEntry_) {
            pathEntry_->setFocus();
            pathEntry_->selectAll();
        }
        else if (pathBar_) {
            pathBar_->openEditor();
        }
    }
}

}  // namespace Oneg4FM
