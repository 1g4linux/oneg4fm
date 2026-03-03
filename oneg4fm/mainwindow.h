/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef FM_MAIN_WINDOW_H
#define FM_MAIN_WINDOW_H

#include "panel/panel.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>

#include "launcher.h"
#include "mainwindow_bookmark_commands.h"
#include "mainwindow_commands.h"
#include "mainwindow_fileops_commands.h"
#include "mainwindow_navigation_commands.h"
#include "mainwindow_selection_commands.h"
#include "mainwindow_tab_commands.h"
#include "mainwindow_view_controller.h"
#include "mainwindow_view_commands.h"
#include "tabbar.h"
#include "tabpage.h"
#include "ui_main-win.h"

namespace Oneg4FM {

class ViewFrame : public QFrame {
    Q_OBJECT
   public:
    ViewFrame(QWidget* parent = nullptr);
    ~ViewFrame() {};

    void createTopBar(bool usePathButtons);
    void removeTopBar();

    QWidget* getTopBar() const { return topBar_; }
    TabBar* getTabBar() const { return tabBar_; }
    QStackedWidget* getStackedWidget() const { return stackedWidget_; }

   private:
    QWidget* topBar_;
    TabBar* tabBar_;
    QStackedWidget* stackedWidget_;
};

//======================================================================

class Settings;

class MainWindow : public QMainWindow,
                   public MainWindowBookmarkCommands::Context,
                   public MainWindowCommands::Context,
                   public MainWindowFileOpsCommands::Context,
                   public MainWindowNavigationCommands::Context,
                   public MainWindowSelectionCommands::Context,
                   public MainWindowTabCommands::Context,
                   public MainWindowViewController::Context,
                   public MainWindowViewCommands::Context {
    Q_OBJECT
   public:
    MainWindow(Panel::FilePath path = Panel::FilePath());
    virtual ~MainWindow();

    void chdir(Panel::FilePath path, ViewFrame* viewFrame);
    void chdir(Panel::FilePath path) { chdir(path, activeViewFrame_); }

    void addTab(Panel::FilePath path, ViewFrame* viewFrame);
    void addTab(Panel::FilePath path);

    TabPage* currentPage(ViewFrame* viewFrame) {
        return reinterpret_cast<TabPage*>(viewFrame->getStackedWidget()->currentWidget());
    }
    TabPage* currentPage() { return currentPage(activeViewFrame_); }
    TabPage* currentPage(ViewFrame* viewFrame) const {
        return reinterpret_cast<TabPage*>(viewFrame->getStackedWidget()->currentWidget());
    }
    TabPage* currentPage() const { return currentPage(activeViewFrame_); }

    void updateFromSettings(Settings& settings);

    static MainWindow* lastActive() { return lastActive_; }

    void openFolderAndSelectFiles(const Panel::FilePathList& files, bool inNewTab = true);

   protected Q_SLOTS:

    void onPathEntryReturnPressed();
    void onPathBarChdir(const Panel::FilePath& dirPath);
    void onPathBarMiddleClickChdir(const Panel::FilePath& dirPath);

    void on_actionNewTab_triggered();
    void on_actionNewWin_triggered();
    void on_actionNewFolder_triggered();
    void on_actionNewBlankFile_triggered();
    void on_actionCloseTab_triggered();
    void on_actionCloseWindow_triggered();
    void on_actionFileProperties_triggered();
    void on_actionFolderProperties_triggered();

    void on_actionCut_triggered();
    void on_actionCopy_triggered();
    void on_actionPaste_triggered();
    void on_actionDelete_triggered();
    void on_actionRename_triggered();
    void on_actionBulkRename_triggered();
    void on_actionSelectAll_triggered();
    void on_actionDeselectAll_triggered();
    void on_actionInvertSelection_triggered();
    void on_actionPreferences_triggered();

    void on_actionGoBack_triggered();
    void on_actionGoForward_triggered();
    void on_actionGoUp_triggered();
    void on_actionHome_triggered();
    void on_actionReload_triggered();

    void on_actionIconView_triggered();
    void on_actionCompactView_triggered();
    void on_actionDetailedList_triggered();
    void on_actionThumbnailView_triggered();

    void on_actionGo_triggered();
    void on_actionShowHidden_triggered(bool check);
    void on_actionShowThumbnails_triggered(bool check);
    void on_actionSplitView_triggered(bool check);
    void on_actionPreserveView_triggered(bool checked);
    void on_actionPreserveViewRecursive_triggered(bool checked);
    void on_actionGoToCustomizedViewSource_triggered();

    void on_actionByFileName_triggered(bool checked);
    void on_actionByMTime_triggered(bool checked);
    void on_actionByCrTime_triggered(bool checked);
    void on_actionByDTime_triggered(bool checked);
    void on_actionByOwner_triggered(bool checked);
    void on_actionByGroup_triggered(bool checked);
    void on_actionByFileType_triggered(bool checked);
    void on_actionByFileSize_triggered(bool checked);
    void on_actionAscending_triggered(bool checked);
    void on_actionDescending_triggered(bool checked);
    void on_actionFolderFirst_triggered(bool checked);
    void on_actionCaseSensitive_triggered(bool checked);
    void on_actionFilter_triggered(bool checked);
    void on_actionUnfilter_triggered();
    void on_actionShowFilter_triggered();

    void on_actionLocationBar_triggered(bool checked);
    void on_actionPathButtons_triggered(bool checked);

    void on_actionApplications_triggered();
    void on_actionTrash_triggered();
    void on_actionDesktop_triggered();
    void on_actionAddToBookmarks_triggered();
    void on_actionEditBookmarks_triggered();

    void on_actionOpenTerminal_triggered();
    void on_actionCreateLauncher_triggered();
    void on_actionCopyFullPath_triggered();
    void on_actionFindFiles_triggered();

    void on_actionAbout_triggered();
    void on_actionHiddenShortcuts_triggered();

    void onTabBarCloseRequested(int index);
    void onTabBarCurrentChanged(int index);
    void onTabBarTabMoved(int from, int to);

    void onShortcutPrevTab();
    void onShortcutNextTab();
    void onShortcutJumpToTab();

    void onStackedWidgetWidgetRemoved(int index);

    void onTabPageTitleChanged();
    void onTabPageStatusChanged(int type, QString statusText);
    void onTabPageSortFilterChanged();

    void onSidePaneChdirRequested(int type, const Panel::FilePath& path);
    void onSidePaneOpenFolderInNewWindowRequested(const Panel::FilePath& path);
    void onSidePaneOpenFolderInNewTabRequested(const Panel::FilePath& path);
    void onSidePaneOpenFolderInTerminalRequested(const Panel::FilePath& path);
    void onSidePaneCreateNewFolderRequested(const Panel::FilePath& path);
    void onSidePaneModeChanged(Panel::SidePane::Mode mode);
    void on_actionSidePane_triggered(bool check);
    void onSplitterMoved(int pos, int index);
    void onResetFocus();

    void onBackForwardContextMenu(QPoint pos);

    void onFolderUnmounted();

    void tabContextMenu(const QPoint& pos);
    void onTabBarClicked(int index);
    void closeLeftTabs();
    void closeRightTabs();
    void closeOtherTabs();
    void focusPathEntry();
    void toggleMenuBar(bool checked);
    void on_actionMenu_triggered();
    void detachTab();

    void onBookmarksChanged();

    void onSettingHiddenPlace(const QString& str, bool hide);

    void on_actionCleanPerFolderConfig_triggered();

   protected:
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeTab(int index, ViewFrame* viewFrame);
    void closeTab(int index) { closeTab(index, activeViewFrame_); }
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void moveEvent(QMoveEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void openNewTab() override;
    void openNewWindow() override;
    bool hasActiveTab() const override;
    void closeActiveTab() override;
    void closeWindow() override;
    void openPreferences() override;
    void editBookmarks() override;
    void showAboutDialog() override;

    void openBookmarkInCurrentTab(const QString& bookmarkPath) override;
    void openBookmarkInNewTab(const QString& bookmarkPath) override;
    void openBookmarkInNewWindow(const QString& bookmarkPath) override;
    void openBookmarkInLastActiveWindow(const QString& bookmarkPath) override;

    bool hasCurrentPage() const override;
    bool hasDesktopPath() const override;
    void navigateUp() override;
    void navigateBack() override;
    void navigateForward() override;
    void navigateHome() override;
    void reloadCurrent() override;
    void openApplicationsRoot() override;
    void openTrashRoot() override;
    void openDesktopRoot() override;
    void findFilesFromSelection() override;
    void openTerminalAtCurrent() override;

    bool hasSingleSelectedPath() const override;
    bool hasSelectedFiles() const override;
    bool hasAccessibleSelection() const override;
    bool hasDeletableSelection() const override;
    bool canPasteIntoCurrentFolder() const override;
    int renamableSelectionCount() const override;
    void showFileProperties() override;
    void showFolderProperties() override;
    void copySelectionToClipboard() override;
    void cutSelectionToClipboard() override;
    void pasteClipboardIntoCurrentFolder() override;
    void deleteSelection() override;
    void renameSelection() override;
    void bulkRenameSelection() override;
    void selectAllFiles() override;
    void deselectAllFiles() override;
    void invertFileSelection() override;
    void copySelectedPathToClipboard() override;

    int currentTabIndex() const override;
    int tabCount() const override;
    void setCurrentTabIndex(int index) override;
    void closeTabAt(int index) override;
    void focusCurrentTabView() override;

    void setIconMode() override;
    void setCompactMode() override;
    void setDetailedMode() override;
    void setThumbnailMode() override;
    void sortAscending() override;
    void sortDescending() override;
    void sortByFileName() override;
    void sortByMTime() override;
    void sortByCrTime() override;
    void sortByDTime() override;
    void sortByOwner() override;
    void sortByGroup() override;
    void sortByFileSize() override;
    void sortByFileType() override;

    ViewFrame* activeViewFrame() const override;
    void applyViewMode(MainWindowViewController::ViewMode mode) override;
    void applySort(MainWindowViewController::SortColumn column, Qt::SortOrder order) override;
    Qt::SortOrder currentSortOrder() const override;
    MainWindowViewController::SortColumn currentSortColumn() const override;
    void applySortCaseSensitive(bool enabled) override;
    void applySortFolderFirst(bool enabled) override;
    void setFilterBarsPersistent(bool enabled) override;
    void clearFiltersInCurrentWindow() override;
    void showCurrentFilterBar() override;
    void syncPathBarForFrame(ViewFrame* frame) override;
    void projectCurrentPageUi(bool setFocus) override;

    template <typename Func>
    void forEachTabPageLocal(Func func);

    template <typename Func>
    void forEachTabPageGlobal(Func func);

    void handleFocusIn(QWidget* watchedWidget);
    bool handleTabKey(QKeyEvent* ke, QWidget* watchedWidget);

    void loadBookmarksMenu();
    void updateUIForCurrentPage(bool setFocus = true);
    void updateViewMenuForCurrentPage();
    void updateSelectedActions();
    void updateStatusBarForCurrentPage();
    void setRTLIcons(bool isRTL);
    void createPathBar(bool usePathButtons);
    void createSplitViewPathBar(bool usePathButtons);
    void createSingleViewPathBar(bool usePathButtons);
    void addViewFrame(const Panel::FilePath& path);
    ViewFrame* viewFrameForTabPage(TabPage* page);
    int addTabWithPage(TabPage* page, ViewFrame* viewFrame, Panel::FilePath path = Panel::FilePath());
    void dropTab(QObject* source);
    void applyFrameActivation(ViewFrame* newActiveFrame);
    void setTabIcon(TabPage* tabPage);

   private:
    Ui::MainWindow ui;
    Panel::PathEdit* pathEntry_;
    Panel::PathBar* pathBar_;
    QLabel* fsInfoLabel_;
    std::shared_ptr<Panel::Bookmarks> bookmarks_;
    Launcher fileLauncher_;
    int rightClickIndex_;
    bool updatingViewMenu_;
    QAction* menuSep_;
    QAction* menuSpacer_;

    ViewFrame* activeViewFrame_;
    // The split mode of this window is changed only from its settings,
    // not from another window. So, we get the mode at the start and keep it.
    bool splitView_;

    int splitTabsNum_;  // number of tabs to be restored from the first view frame of the last window

    static QPointer<MainWindow> lastActive_;
};

}  // namespace Oneg4FM

#endif  // FM_MAIN_WINDOW_H
