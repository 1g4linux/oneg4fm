/*
 * Main window tab management implementation
 * oneg4fm/mainwindow_tabs.cpp
 */

#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTimer>

#include "application.h"
#include "mainwindow.h"
#include "tabbar.h"  // Assuming custom TabBar header
#include "tabpage.h"
#include "view.h"  // Assuming View definition is needed for access

// If using the modernization plan, ensure these headers match the new architecture.
// For now, retaining Fm headers to maintain compatibility with the provided context.
#include "panel/panel.h"

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

void MainWindow::addViewFrame(const Panel::FilePath& path) {
    ui.actionGo->setVisible(false);

    Settings& settings = appSettings();
    auto* viewFrame = new ViewFrame();
    auto* tabBar = viewFrame->getTabBar();

    tabBar->setDetachable(!splitView_);  // no tab DND with the split view
    tabBar->setTabsClosable(settings.showTabClose());
    tabBar->setAutoHide(!settings.alwaysShowTabs());

    // The splitter takes ownership of viewFrame
    ui.viewSplitter->addWidget(viewFrame);

    if (ui.viewSplitter->count() == 1) {
        applyFrameActivation(viewFrame);
    }
    else {
        // Equalize sizes after the event loop processes the new widget addition
        QTimer::singleShot(0, this, [this] {
            const int count = ui.viewSplitter->count();
            if (count > 0) {
                const int widthPerFrame = ui.viewSplitter->width() / count;
                QList<int> sizes(count, widthPerFrame);
                ui.viewSplitter->setSizes(sizes);
            }
            applyFrameActivation(activeViewFrame_);
        });
    }

    connect(tabBar, &QTabBar::currentChanged, this, &MainWindow::onTabBarCurrentChanged);
    connect(tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::onTabBarCloseRequested);
    connect(tabBar, &QTabBar::tabMoved, this, &MainWindow::onTabBarTabMoved);
    connect(tabBar, &QTabBar::tabBarClicked, this, &MainWindow::onTabBarClicked);
    connect(tabBar, &QTabBar::customContextMenuRequested, this, &MainWindow::tabContextMenu);

    connect(tabBar, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index == -1) {
            on_actionNewTab_triggered();
        }
    });

    connect(viewFrame->getStackedWidget(), &QStackedWidget::widgetRemoved, this,
            &MainWindow::onStackedWidgetWidgetRemoved);

    // the tab will be detached only after the DND is finished
    connect(tabBar, &TabBar::tabDetached, this, &MainWindow::detachTab, Qt::QueuedConnection);

    if (path) {
        addTab(path, viewFrame);
    }
}

int MainWindow::addTabWithPage(TabPage* page, ViewFrame* viewFrame, Panel::FilePath path) {
    if (!page || !viewFrame) {
        return -1;
    }

    page->setFileLauncher(&fileLauncher_);
    auto* stackedWidget = viewFrame->getStackedWidget();
    const int index = stackedWidget->addWidget(page);

    connect(page, &TabPage::titleChanged, this, &MainWindow::onTabPageTitleChanged);
    connect(page, &TabPage::statusChanged, this, &MainWindow::onTabPageStatusChanged);
    connect(page, &TabPage::sortFilterChanged, this, &MainWindow::onTabPageSortFilterChanged);
    connect(page, &TabPage::backwardRequested, this, &MainWindow::on_actionGoBack_triggered);
    connect(page, &TabPage::forwardRequested, this, &MainWindow::on_actionGoForward_triggered);
    connect(page, &TabPage::backspacePressed, this, &MainWindow::on_actionGoUp_triggered);
    connect(page, &TabPage::folderUnmounted, this, &MainWindow::onFolderUnmounted);

    if (path) {
        page->chdir(path, true);
    }

    QString tabText = page->title();
    // remove newline (not all styles can handle it) and distinguish ampersand from mnemonic
    tabText.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('&'), QStringLiteral("&&"));

    auto* tabBar = viewFrame->getTabBar();
    tabBar->insertTab(index, tabText);

    Settings& settings = appSettings();

    if (settings.switchToNewTab()) {
        tabBar->setCurrentIndex(index);  // also focuses the view
        if (isMinimized()) {
            setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            show();
        }
    }
    else if (TabPage* tabPage = currentPage()) {
        // Keep focus on the current page if we aren't switching
        tabPage->folderView()->childView()->setFocus();
    }

    // also set tab icon (if the folder is customized)
    setTabIcon(page);

    return index;
}

// add a new tab
void MainWindow::addTab(Panel::FilePath path, ViewFrame* viewFrame) {
    auto* newPage = new TabPage(this);
    addTabWithPage(newPage, viewFrame, path);
}

void MainWindow::addTab(Panel::FilePath path) {
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    // Handle restoring session tabs in split view mode
    if (splitView_ && app->openingLastTabs()) {
        int N = app->settings().splitViewTabsNum();
        if (N > 0) {
            // Divide tabs between the first and second view frames appropriately.
            // It is assumed that reopening of last tabs is started when the split view has
            // two frames, each with a single tab created in the ctor.
            if (splitTabsNum_ == -1) {
                splitTabsNum_ = N - 1;
            }

            auto* firstFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(0));
            auto* secondFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(1));

            if (!firstFrame || !secondFrame) {
                // Fallback: something is wrong with the splitter state
                app->settings().setSplitViewTabsNum(0);
                splitTabsNum_ = -1;
                addTab(path, activeViewFrame_);
            }
            else if (splitTabsNum_ > 0) {
                --splitTabsNum_;
                addTab(path, firstFrame);
            }
            else {
                addTab(path, secondFrame);
                // On reaching the single tab of the second frame, remove it after adding a tab
                // to avoid keeping the default empty tab open.
                if (splitTabsNum_ == 0 && secondFrame->getStackedWidget()->count() == 2) {
                    closeTab(0, secondFrame);
                }
                splitTabsNum_ = -2;  // will not change again for the current window
            }
            return;
        }
    }

    // Default behavior: add the tab to the active view frame
    addTab(path, activeViewFrame_);
}

void MainWindow::closeTab(int index, ViewFrame* viewFrame) {
    auto* stackedWidget = viewFrame->getStackedWidget();
    QWidget* page = stackedWidget->widget(index);
    if (page) {
        // removeWidget emits widgetRemoved, which calls onStackedWidgetWidgetRemoved
        stackedWidget->removeWidget(page);
        // Use deleteLater to prevent crashes if the tab is processing an event
        page->deleteLater();
    }
}

void MainWindow::onTabBarCloseRequested(int index) {
    auto* tabBar = qobject_cast<TabBar*>(sender());
    if (!tabBar)
        return;

    if (auto* viewFrame = qobject_cast<ViewFrame*>(tabBar->parentWidget())) {
        closeTab(index, viewFrame);
    }
}

void MainWindow::onTabBarCurrentChanged(int index) {
    auto* tabBar = qobject_cast<TabBar*>(sender());
    if (!tabBar)
        return;

    auto* viewFrame = qobject_cast<ViewFrame*>(tabBar->parentWidget());
    if (!viewFrame)
        return;

    auto* stackedWidget = viewFrame->getStackedWidget();
    stackedWidget->setCurrentIndex(index);
    // Keep selection exclusive to the visible tab within this frame
    for (int i = 0; i < stackedWidget->count(); ++i) {
        if (i == index) {
            continue;
        }
        if (auto* page = qobject_cast<TabPage*>(stackedWidget->widget(i))) {
            page->deselectAll();
        }
    }

    if (viewFrame == activeViewFrame_) {
        updateUIForCurrentPage();
    }
    else {
        // If updating a background frame, strictly update its location bar, not the main window UI
        if (TabPage* page = currentPage(viewFrame)) {
            if (auto* pathBar = qobject_cast<Panel::PathBar*>(viewFrame->getTopBar())) {
                pathBar->setPath(page->path());
            }
            else if (auto* pathEntry = qobject_cast<Panel::PathEdit*>(viewFrame->getTopBar())) {
                pathEntry->setText(page->pathName());
            }
        }
    }
}

void MainWindow::onTabBarTabMoved(int from, int to) {
    auto* tabBar = qobject_cast<TabBar*>(sender());
    if (!tabBar)
        return;

    auto* viewFrame = qobject_cast<ViewFrame*>(tabBar->parentWidget());
    if (!viewFrame)
        return;

    auto* stackedWidget = viewFrame->getStackedWidget();
    QWidget* page = stackedWidget->widget(from);
    if (!page)
        return;

    // block signals to avoid onStackedWidgetWidgetRemoved() being called while reshuffling pages
    const bool previousBlockState = stackedWidget->blockSignals(true);
    stackedWidget->removeWidget(page);
    stackedWidget->insertWidget(to, page);  // insert the page to the new position
    stackedWidget->blockSignals(previousBlockState);
    stackedWidget->setCurrentWidget(page);
}

void MainWindow::onStackedWidgetWidgetRemoved(int index) {
    auto* sw = qobject_cast<QStackedWidget*>(sender());
    if (!sw)
        return;

    auto* viewFrame = qobject_cast<ViewFrame*>(sw->parentWidget());
    if (!viewFrame)
        return;

    auto* tabBar = viewFrame->getTabBar();
    tabBar->removeTab(index);

    // If tabs remain, we are done
    if (tabBar->count() != 0) {
        return;
    }

    // Use deleteLater to ensure all pending events for the frame are processed
    if (!splitView_) {
        deleteLater();  // destroy the whole window
    }
    else {
        // In split mode, if the last tab of a view frame is closed,
        // remove that view frame and revert to simple mode.

        // Find the "other" view frame to make active
        ViewFrame* nextViewFrame = nullptr;
        for (int i = 0; i < ui.viewSplitter->count(); ++i) {
            auto* candidate = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));
            if (candidate && candidate != viewFrame) {
                nextViewFrame = candidate;
                break;
            }
        }

        if (nextViewFrame && activeViewFrame_ != nextViewFrame) {
            activeViewFrame_ = nextViewFrame;
            updateUIForCurrentPage();

            // if the window isn't active, eventFilter() won't be called,
            // so we should revert to the main palette here manually
            if (activeViewFrame_->palette().color(QPalette::Base) != qApp->palette().color(QPalette::Base)) {
                activeViewFrame_->setPalette(qApp->palette());
            }
        }

        ui.actionSplitView->setChecked(false);
        on_actionSplitView_triggered(false);
    }
}

ViewFrame* MainWindow::viewFrameForTabPage(TabPage* page) {
    if (!page)
        return nullptr;

    auto* sw = qobject_cast<QStackedWidget*>(page->parentWidget());
    if (!sw)
        return nullptr;

    return qobject_cast<ViewFrame*>(sw->parentWidget());
}

void MainWindow::setTabIcon(TabPage* tabPage) {
    ViewFrame* viewFrame = viewFrameForTabPage(tabPage);
    if (!viewFrame)
        return;

    const bool isCustomized = tabPage->hasCustomizedView() || tabPage->hasInheritedCustomizedView();
    auto* tabBar = viewFrame->getTabBar();
    const int index = viewFrame->getStackedWidget()->indexOf(tabPage);

    if (!isCustomized) {
        if (!tabBar->tabIcon(index).isNull()) {
            tabBar->setTabIcon(index, QIcon());
        }
        return;
    }

    // set the tab icon of a customized folder to its view mode
    QString iconName;
    QStyle::StandardPixmap standardIcon = QStyle::SP_CustomBase;

    switch (tabPage->viewMode()) {
        case Panel::FolderView::IconMode:
            iconName = QStringLiteral("view-list-icons");
            standardIcon = QStyle::SP_FileDialogContentsView;
            break;
        case Panel::FolderView::CompactMode:
            iconName = QStringLiteral("view-list-text");
            standardIcon = QStyle::SP_FileDialogListView;
            break;
        case Panel::FolderView::DetailedListMode:
            iconName = QStringLiteral("view-list-details");
            standardIcon = QStyle::SP_FileDialogDetailedView;
            break;
        case Panel::FolderView::ThumbnailMode:
            iconName = QStringLiteral("view-preview");
            standardIcon = QStyle::SP_FileDialogInfoView;
            break;
    }

    if (!iconName.isEmpty()) {
        tabBar->setTabIcon(index, QIcon::fromTheme(iconName, style()->standardIcon(standardIcon)));
    }
}

}  // namespace Oneg4FM
