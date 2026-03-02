/*
 * Main window implementation for oneg4fm
 * oneg4fm/mainwindow.cpp
 */

#include "mainwindow.h"

#include "createlauncherdialog.h"
#include "hiddenshortcutsdialog.h"
#include "application.h"
#include "settings.h"
#include "tabpage.h"

// Qt Headers
#include <QActionGroup>
#include <QApplication>
#include <QDir>   // <--- Added for QDir
#include <QFile>  // <--- Added for QFile
#include <QFrame>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSizePolicy>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <unistd.h>
#include "../src/ui/fsqt.h"
#include "../src/ui/visual_style.h"

// LibFM-Qt Headers
#include "panel/panel.h"

namespace Oneg4FM {

QPointer<MainWindow> MainWindow::lastActive_;

namespace {

Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

MainWindow::MainWindow(Panel::FilePath path)
    : pathEntry_(nullptr),
      pathBar_(nullptr),
      fsInfoLabel_(nullptr),
      fileLauncher_(this),
      rightClickIndex_(-1),
      updatingViewMenu_(false),
      menuSep_(nullptr),
      menuSpacer_(nullptr),
      activeViewFrame_(nullptr),
      splitView_(false),
      splitTabsNum_(0) {
    ui.setupUi(this);

    // Initialize Back/Forward buttons context menu policy
    ui.actionGoBack->setMenuRole(QAction::NoRole);

    // Ensure Delete shortcut works even when nested views have focus
    ui.actionDelete->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &MainWindow::on_actionDelete_triggered);

    fsInfoLabel_ = new QLabel(this);
    fsInfoLabel_->setFrameShape(QFrame::NoFrame);
    fsInfoLabel_->setContentsMargins(4, 0, 4, 0);
    fsInfoLabel_->setVisible(false);

    if (ui.statusbar) {
        ui.statusbar->setSizeGripEnabled(false);

        if (geteuid() == 0) {
            auto* rootWarning = new QLabel(tr("Running as root. Privilege escalation is disabled."), this);
            rootWarning->setObjectName(QStringLiteral("rootWarningLabel"));
            rootWarning->setAlignment(Qt::AlignCenter);
            rootWarning->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            UiStyle::applyWarningLabel(rootWarning);
            ui.statusbar->addPermanentWidget(rootWarning, 1);
        }

        ui.statusbar->addPermanentWidget(fsInfoLabel_);
    }

    Settings& settings = appSettings();

    // Initialize side pane
    if (ui.sidePane) {
        ui.sidePane->setVisible(settings.isSidePaneVisible());
        if (ui.actionSidePane) {
            ui.actionSidePane->setChecked(settings.isSidePaneVisible());
        }
        ui.sidePane->setIconSize(QSize(settings.sidePaneIconSize(), settings.sidePaneIconSize()));
        ui.sidePane->setMode(settings.sidePaneMode());
        ui.sidePane->restoreHiddenPlaces(settings.getHiddenPlaces());

        connect(ui.sidePane, &Panel::SidePane::chdirRequested, this, &MainWindow::onSidePaneChdirRequested);
        connect(ui.sidePane, &Panel::SidePane::openFolderInNewWindowRequested, this,
                &MainWindow::onSidePaneOpenFolderInNewWindowRequested);
        connect(ui.sidePane, &Panel::SidePane::openFolderInNewTabRequested, this,
                &MainWindow::onSidePaneOpenFolderInNewTabRequested);
        connect(ui.sidePane, &Panel::SidePane::openFolderInTerminalRequested, this,
                &MainWindow::onSidePaneOpenFolderInTerminalRequested);
        connect(ui.sidePane, &Panel::SidePane::createNewFolderRequested, this,
                &MainWindow::onSidePaneCreateNewFolderRequested);
        connect(ui.sidePane, &Panel::SidePane::modeChanged, this, &MainWindow::onSidePaneModeChanged);
        connect(ui.sidePane, &Panel::SidePane::hiddenPlaceSet, this, &MainWindow::onSettingHiddenPlace);
    }

    // Initialize splitter
    if (ui.splitter) {
        connect(ui.splitter, &QSplitter::splitterMoved, this, &MainWindow::onSplitterMoved);
        ui.splitter->setStretchFactor(1, 1);
        ui.splitter->setSizes({settings.splitterPos(), 1});
    }

    // Initialize standard view action icons
    auto setActionIcon = [this](QAction* action, const QString& themeIcon, QStyle::StandardPixmap standardIcon) {
        if (action) {
            action->setIcon(QIcon::fromTheme(themeIcon, style()->standardIcon(standardIcon)));
        }
    };

    // View mode actions should be mutually exclusive
    auto* viewModeGroup = new QActionGroup(this);
    viewModeGroup->setExclusive(true);
    viewModeGroup->addAction(ui.actionIconView);
    viewModeGroup->addAction(ui.actionCompactView);
    viewModeGroup->addAction(ui.actionDetailedList);
    viewModeGroup->addAction(ui.actionThumbnailView);

    // Sorting column actions should be mutually exclusive
    auto* sortColumnGroup = new QActionGroup(this);
    sortColumnGroup->setExclusive(true);
    sortColumnGroup->addAction(ui.actionByFileName);
    sortColumnGroup->addAction(ui.actionByMTime);
    sortColumnGroup->addAction(ui.actionByCrTime);
    sortColumnGroup->addAction(ui.actionByDTime);
    sortColumnGroup->addAction(ui.actionByFileSize);
    sortColumnGroup->addAction(ui.actionByFileType);
    sortColumnGroup->addAction(ui.actionByOwner);
    sortColumnGroup->addAction(ui.actionByGroup);

    auto* sortOrderGroup = new QActionGroup(this);
    sortOrderGroup->setExclusive(true);
    sortOrderGroup->addAction(ui.actionAscending);
    sortOrderGroup->addAction(ui.actionDescending);

    setActionIcon(ui.actionIconView, QStringLiteral("view-list-icons"), QStyle::SP_FileDialogContentsView);
    setActionIcon(ui.actionThumbnailView, QStringLiteral("view-list-icons"), QStyle::SP_FileDialogContentsView);
    setActionIcon(ui.actionCompactView, QStringLiteral("view-list-details"), QStyle::SP_FileDialogDetailedView);
    setActionIcon(ui.actionDetailedList, QStringLiteral("view-list-details"), QStyle::SP_FileDialogDetailedView);

    updateFromSettings(settings);

    // Add initial view frame
    addViewFrame(path);
    applyFrameActivation(activeViewFrame_);

    // Create the path bar/location bar
    createPathBar(appSettings().pathBarButtons());

    // Insert an expanding spacer to push subsequent items (like actionMenu) to the right
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui.toolBar->insertWidget(ui.actionMenu, spacer);

    setWindowTitle(QStringLiteral("oneg4fm"));

    connect(ui.actionQuit, &QAction::triggered, qApp, &QApplication::quit);

    lastActive_ = this;
}

MainWindow::~MainWindow() {
    if (lastActive_ == this) {
        lastActive_ = nullptr;
    }
}

//-----------------------------------------------------------------------------
// File Menu Actions
//-----------------------------------------------------------------------------

void MainWindow::on_actionNewTab_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::NewTab, *this);
}

void MainWindow::on_actionNewWin_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::NewWindow, *this);
}

void MainWindow::openNewTab() {
    Panel::FilePath path;
    if (TabPage* page = currentPage()) {
        path = page->path();
    }
    else {
        path = Panel::FilePath::homeDir();
    }

    addTab(path);
}

void MainWindow::openNewWindow() {
    Panel::FilePath path;
    if (TabPage* page = currentPage()) {
        path = page->path();
    }
    else {
        path = Panel::FilePath::homeDir();
    }

    auto* win = new MainWindow(path);
    Settings& settings = appSettings();
    win->resize(settings.windowWidth(), settings.windowHeight());
    if (settings.windowX() != -1 && settings.windowY() != -1) {
        win->move(settings.windowX(), settings.windowY());
    }
    win->show();
}

void MainWindow::on_actionNewFolder_triggered() {
    if (TabPage* page = currentPage()) {
        bool ok = false;

        QString name =

            QInputDialog::getText(this, tr("New Folder"), tr("Folder Name:"), QLineEdit::Normal, tr("New Folder"), &ok);

        if (!ok || name.isEmpty())
            return;

        // Use Qt for local file operations (Modernization Goal)

        if (page->path().isNative()) {
            QString dirPath = QString::fromUtf8(page->path().localPath().get());

            QDir dir(dirPath);

            if (!dir.mkdir(name)) {
                QMessageBox::warning(this, tr("Error"), tr("Failed to create folder \"%1\"").arg(name));
            }
        }
        else {
            QMessageBox::warning(this, tr("Operation Not Supported"),

                                 tr("Creating folders is currently supported only on local file systems."));
        }
    }
}

void MainWindow::on_actionNewBlankFile_triggered() {
    if (TabPage* page = currentPage()) {
        bool ok = false;
        QString name =
            QInputDialog::getText(this, tr("New File"), tr("File Name:"), QLineEdit::Normal, tr("New File"), &ok);
        if (!ok || name.isEmpty())
            return;

        // Verify we are dealing with a local path
        if (!page->path().isNative()) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Creating blank files is currently supported only on local file systems."));
            return;
        }

        // Convert Panel::FilePath to a local filesystem path string
        // localPath() returns a CStrPtr, get() gives the const char*
        QString dirPath = QString::fromUtf8(page->path().localPath().get());
        QDir dir(dirPath);

        QString filePath = dir.filePath(name);
        QString error;
        if (!FsQt::writeFileAtomic(filePath, QByteArray(), error)) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to create file \"%1\": %2").arg(name, error));
            return;
        }
    }
}

void MainWindow::on_actionCreateLauncher_triggered() {
    if (TabPage* page = currentPage()) {
        CreateLauncherDialog dialog(this);  // No qualifier needed
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.launcherName();
            QString command = dialog.launcherCommand();

            if (name.isEmpty() || command.isEmpty()) {
                QMessageBox::warning(this, tr("Create Launcher Failed"), tr("Name and Command cannot be empty."));
                return;
            }

            // Correctly convert Panel::FilePath to a local file path string
            QString currentDirPath = QString::fromUtf8(page->path().localPath().get());
            QString filePath = currentDirPath + QStringLiteral("/") + name + QStringLiteral(".desktop");
            QString error;
            QString content = QStringLiteral(
                                  "[Desktop Entry]\n"
                                  "Version=1.0\n"
                                  "Type=Application\n"
                                  "Name=%1\n"
                                  "Exec=%2\n"
                                  "Terminal=false\n")
                                  .arg(name, command);
            if (FsQt::writeFileAtomic(filePath, content.toUtf8(), error)) {
                QMessageBox::information(this, tr("Launcher Created"),
                                         tr("Launcher '%1.desktop' created successfully.").arg(name));
            }
            else {
                QMessageBox::warning(this, tr("Create Launcher Failed"),
                                     tr("Failed to create launcher file '%1': %2").arg(name, error));
            }
        }
    }
}

void MainWindow::on_actionCloseWindow_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::CloseWindow, *this);
}

void MainWindow::on_actionCloseTab_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::CloseTab, *this);
}

void MainWindow::on_actionPreferences_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::Preferences, *this);
}

void MainWindow::on_actionEditBookmarks_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::EditBookmarks, *this);
}

void MainWindow::on_actionAbout_triggered() {
    MainWindowCommands::execute(MainWindowCommands::Id::About, *this);
}

bool MainWindow::hasActiveTab() const {
    return activeViewFrame_ && activeViewFrame_->getStackedWidget() &&
           activeViewFrame_->getStackedWidget()->currentIndex() >= 0;
}

void MainWindow::closeActiveTab() {
    if (!activeViewFrame_ || !activeViewFrame_->getStackedWidget()) {
        return;
    }

    const int currentIndex = activeViewFrame_->getStackedWidget()->currentIndex();
    if (currentIndex >= 0) {
        closeTab(currentIndex, activeViewFrame_);
    }
}

void MainWindow::closeWindow() {
    close();
}

void MainWindow::openPreferences() {
    static_cast<Application*>(qApp)->preferences(QString());
}

void MainWindow::editBookmarks() {
    static_cast<Application*>(qApp)->editBookmarks();
}

void MainWindow::showAboutDialog() {
    QMessageBox::about(this, tr("About oneg4fm"),
                       tr("oneg4fm is a file manager.\n\n"
                          "Copyright (C) 2013-2024 oneg4fm Team\n"));
}

//-----------------------------------------------------------------------------
// View Menu Toggles
//-----------------------------------------------------------------------------

void MainWindow::on_actionSplitView_triggered(bool check) {
    if (check == splitView_) {
        return;
    }

    splitView_ = check;
    appSettings().setSplitView(check);

    if (splitView_) {
        // Enable Split: Add a second ViewFrame
        Panel::FilePath path;
        if (TabPage* page = currentPage()) {
            path = page->path();
        }
        else {
            path = Panel::FilePath::homeDir();
        }

        addViewFrame(path);
        createPathBar(appSettings().pathBarButtons());

        if (ui.viewSplitter->count() > 1) {
            activeViewFrame_ = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(1));
            updateUIForCurrentPage();
        }
    }
    else {
        // Disable Split: Remove the second ViewFrame
        if (ui.viewSplitter->count() > 1) {
            auto* secondFrame = ui.viewSplitter->widget(1);
            secondFrame->deleteLater();

            activeViewFrame_ = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(0));
            createPathBar(appSettings().pathBarButtons());
            updateUIForCurrentPage();
        }
    }
}

void MainWindow::on_actionSidePane_triggered(bool check) {
    if (ui.sidePane) {
        ui.sidePane->setVisible(check);
        // Persist setting here if your Settings class supports it
    }
}

//-----------------------------------------------------------------------------
// Other event handlers
//-----------------------------------------------------------------------------

void MainWindow::updateFromSettings(Settings& settings) {
    splitView_ = settings.splitView();

    if (ui.sidePane) {
        ui.sidePane->setVisible(settings.isSidePaneVisible());
    }
    if (ui.splitter) {
        ui.splitter->setSizes({settings.splitterPos(), 1});
    }
    if (ui.menubar) {
        ui.menubar->setVisible(settings.showMenuBar());
    }
}

void MainWindow::setRTLIcons(bool isRTL) {
    if (isRTL) {
        ui.actionGoBack->setIcon(
            QIcon::fromTheme(QStringLiteral("go-next"), style()->standardIcon(QStyle::SP_ArrowRight)));
        ui.actionGoForward->setIcon(
            QIcon::fromTheme(QStringLiteral("go-previous"), style()->standardIcon(QStyle::SP_ArrowLeft)));
    }
    else {
        ui.actionGoBack->setIcon(
            QIcon::fromTheme(QStringLiteral("go-previous"), style()->standardIcon(QStyle::SP_ArrowLeft)));
        ui.actionGoForward->setIcon(
            QIcon::fromTheme(QStringLiteral("go-next"), style()->standardIcon(QStyle::SP_ArrowRight)));
    }
}

void MainWindow::onTabPageTitleChanged() {
    auto* page = qobject_cast<TabPage*>(sender());
    if (!page) {
        page = currentPage();
    }

    if (page) {
        if (ViewFrame* viewFrame = viewFrameForTabPage(page)) {
            if (auto* stackedWidget = viewFrame->getStackedWidget()) {
                const int index = stackedWidget->indexOf(page);
                if (index >= 0) {
                    QString tabText = page->title();
                    tabText.replace(QLatin1Char('\n'), QLatin1Char(' '))
                        .replace(QLatin1Char('&'), QStringLiteral("&&"));
                    viewFrame->getTabBar()->setTabText(index, tabText);
                    setTabIcon(page);
                }
            }
        }
    }

    updateUIForCurrentPage();
}

void MainWindow::onTabPageStatusChanged(int type, QString statusText) {
    if (type == TabPage::StatusTextFSInfo) {
        if (fsInfoLabel_) {
            fsInfoLabel_->setText(statusText);
            fsInfoLabel_->setVisible(!statusText.isEmpty());
        }
    }
    else {
        if (ui.statusbar) {
            ui.statusbar->showMessage(statusText);
        }
    }
}

void MainWindow::onTabPageSortFilterChanged() {
    updateViewMenuForCurrentPage();
}

void MainWindow::onFolderUnmounted() {
    updateUIForCurrentPage();
}

void MainWindow::onTabBarClicked(int index) {
    Q_UNUSED(index);
    if (activeViewFrame_) {
        if (TabPage* page = currentPage()) {
            if (page->folderView() && page->folderView()->childView()) {
                page->folderView()->childView()->setFocus();
            }
        }
    }
}

void MainWindow::tabContextMenu(const QPoint& pos) {
    if (!activeViewFrame_)
        return;

    QMenu menu(this);
    menu.addAction(tr("Close Tab"), this, &MainWindow::on_actionCloseTab_triggered);
    menu.addAction(tr("Close Other Tabs"), this, [this] {
        closeLeftTabs();
        closeRightTabs();
    });
    menu.addAction(tr("Close Tabs to Left"), this, &MainWindow::closeLeftTabs);
    menu.addAction(tr("Close Tabs to Right"), this, &MainWindow::closeRightTabs);

    menu.exec(activeViewFrame_->getTabBar()->mapToGlobal(pos));
}

void MainWindow::on_actionHiddenShortcuts_triggered() {
    HiddenShortcutsDialog dialog(this);
    dialog.exec();
}

void MainWindow::onShortcutPrevTab() {
    if (activeViewFrame_) {
        auto* tab = activeViewFrame_->getTabBar();
        int idx = tab->currentIndex();
        if (idx > 0)
            tab->setCurrentIndex(idx - 1);
        else
            tab->setCurrentIndex(tab->count() - 1);  // Wrap
    }
}

void MainWindow::onShortcutNextTab() {
    if (activeViewFrame_) {
        auto* tab = activeViewFrame_->getTabBar();
        int idx = tab->currentIndex();
        if (idx < tab->count() - 1)
            tab->setCurrentIndex(idx + 1);
        else
            tab->setCurrentIndex(0);  // Wrap
    }
}

void MainWindow::onShortcutJumpToTab() {
    // Logic for Alt+1, Alt+2 etc
}

void MainWindow::onSidePaneChdirRequested(int type, const Panel::FilePath& path) {
    if (type == 0) {  // left button
        chdir(path);
    }
    else if (type == 1) {  // middle button
        addTab(path);
    }
    else if (type == 2) {  // new window
        (new MainWindow(path))->show();
    }
}

void MainWindow::onSidePaneOpenFolderInNewWindowRequested(const Panel::FilePath& path) {
    (new MainWindow(path))->show();
}

void MainWindow::onSidePaneOpenFolderInNewTabRequested(const Panel::FilePath& path) {
    addTab(path);
}

void MainWindow::onSidePaneOpenFolderInTerminalRequested(const Panel::FilePath& path) {
    static_cast<Application*>(qApp)->openFolderInTerminal(path);
}

void MainWindow::onSidePaneCreateNewFolderRequested(const Panel::FilePath& path) {
    chdir(path);
    QTimer::singleShot(100, this, &MainWindow::on_actionNewFolder_triggered);
}

void MainWindow::onSidePaneModeChanged(Panel::SidePane::Mode mode) {
    appSettings().setSidePaneMode(mode);
}

void MainWindow::onSplitterMoved(int pos, int index) {
    Q_UNUSED(index);
    appSettings().setSplitterPos(pos);
}

void MainWindow::onBackForwardContextMenu(QPoint pos) {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn)
        return;

    // BrowseHistory access disabled due to private API limitation in libfm-qt6
    Q_UNUSED(pos);
}

void MainWindow::closeLeftTabs() {
    if (!activeViewFrame_)
        return;

    int currentIndex = activeViewFrame_->getStackedWidget()->currentIndex();

    // Close from left-most neighbor (index 0) up to current-1.
    // Close in reverse order to maintain indices
    for (int i = currentIndex - 1; i >= 0; --i) {
        closeTab(i, activeViewFrame_);
    }
}

void MainWindow::closeRightTabs() {
    if (!activeViewFrame_)
        return;

    int currentIndex = activeViewFrame_->getStackedWidget()->currentIndex();
    int count = activeViewFrame_->getStackedWidget()->count();

    // Close from the last tab down to current+1.
    for (int i = count - 1; i > currentIndex; --i) {
        closeTab(i, activeViewFrame_);
    }
}

void MainWindow::onSettingHiddenPlace(const QString& str, bool hide) {
    appSettings().setHiddenPlace(str, hide);
}

}  // namespace Oneg4FM
