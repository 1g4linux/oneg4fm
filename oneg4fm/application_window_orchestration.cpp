/*
 * Application window/dialog orchestration.
 * oneg4fm/application_window_orchestration.cpp
 */

#include "application.h"

#include <algorithm>
#include <unordered_map>

#include <QDir>
#include <QMessageBox>

#include "connectserverdialog.h"
#include "launcher.h"
#include "mainwindow.h"
#include "preferencesdialog.h"
#include "../src/backends/qt/qt_fileinfo.h"
#include "../src/ui/filepropertiesdialog.h"

namespace Oneg4FM {

namespace {

MainWindow* findLastMainWindow(const QWidgetList& windows) {
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        if (auto* mainWindow = qobject_cast<MainWindow*>(*it)) {
            return mainWindow;
        }
    }
    return nullptr;
}

bool hasAnyMainWindow(const QWidgetList& windows) {
    for (const auto* window : windows) {
        if (qobject_cast<const MainWindow*>(window)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void Application::onFindFileAccepted() {
    // FIX: Using dynamic_cast because Panel::FileSearchDialog might miss Q_OBJECT macro
    auto* dlg = dynamic_cast<Panel::FileSearchDialog*>(sender());
    if (!dlg) {
        return;
    }

    // persist search settings for future sessions
    settings_.setSearchNameCaseInsensitive(dlg->nameCaseInsensitive());
    settings_.setsearchContentCaseInsensitive(dlg->contentCaseInsensitive());
    settings_.setSearchNameRegexp(dlg->nameRegexp());
    settings_.setSearchContentRegexp(dlg->contentRegexp());
    settings_.setSearchRecursive(dlg->recursive());
    settings_.setSearchhHidden(dlg->searchhHidden());
    settings_.addNamePattern(dlg->namePattern());
    settings_.addContentPattern(dlg->contentPattern());

    Panel::FilePathList paths;
    paths.emplace_back(dlg->searchUri());

    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::onConnectToServerAccepted() {
    // FIX: Using dynamic_cast for safety here as well
    auto* dlg = dynamic_cast<ConnectServerDialog*>(sender());
    if (!dlg) {
        return;
    }

    const QString uri = dlg->uriText();

    Panel::FilePathList paths;
    paths.push_back(Panel::FilePath::fromUri(uri.toUtf8().constData()));

    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::findFiles(QStringList paths) {
    // launch file searching utility with the given paths as search roots
    auto* dlg = new Panel::FileSearchDialog(paths);
    connect(dlg, &QDialog::accepted, this, &Application::onFindFileAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // restore last used search settings
    dlg->setNameCaseInsensitive(settings_.searchNameCaseInsensitive());
    dlg->setContentCaseInsensitive(settings_.searchContentCaseInsensitive());
    dlg->setNameRegexp(settings_.searchNameRegexp());
    dlg->setContentRegexp(settings_.searchContentRegexp());
    dlg->setRecursive(settings_.searchRecursive());
    dlg->setSearchhHidden(settings_.searchhHidden());
    dlg->addNamePatterns(settings_.namePatterns());
    dlg->addContentPatterns(settings_.contentPatterns());

    dlg->show();
}

void Application::connectToServer() {
    auto* dlg = new ConnectServerDialog();
    connect(dlg, &QDialog::accepted, this, &Application::onConnectToServerAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void Application::launchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs) {
    Panel::FilePathList pathList;
    Panel::FilePath cwdPath;
    QStringList effectivePaths = paths;

    // optionally reopen last session tab paths if no explicit paths were supplied
    openingLastTabs_ = reopenLastTabs && settings_.reopenLastTabs() && !settings_.tabPaths().isEmpty();
    if (openingLastTabs_) {
        effectivePaths = settings_.tabPaths();
        // forget tab paths for subsequent windows until the last related window is closed
        settings_.setTabPaths(QStringList());
    }

    for (const QString& it : std::as_const(effectivePaths)) {
        const QByteArray pathName = it.toLocal8Bit();
        Panel::FilePath path;

        if (pathName == "~") {
            // home directory shortcut
            path = Panel::FilePath::homeDir();
        }
        else if (!pathName.isEmpty() && pathName[0] == '/') {
            // absolute local path
            path = Panel::FilePath::fromLocalPath(pathName.constData());
        }
        else if (pathName.contains(":/")) {
            // URI such as file://, smb://, etc
            path = Panel::FilePath::fromUri(pathName.constData());
        }
        else {
            // relative path, resolved against the caller's working directory
            if (Q_UNLIKELY(!cwdPath)) {
                cwdPath = Panel::FilePath::fromLocalPath(cwd.toLocal8Bit().constData());
            }
            path = cwdPath.relativePath(pathName.constData());
        }

        pathList.push_back(std::move(path));
    }

    if (!inNewWindow && settings_.singleWindowMode()) {
        MainWindow* window = MainWindow::lastActive();

        // if there is no last active window, find the last created MainWindow
        if (window == nullptr) {
            window = findLastMainWindow(topLevelWidgets());
        }

        if (window != nullptr && openingLastTabs_) {
            // other folders have been opened explicitly in this window;
            // restoring the previous split view tab count would be misleading
            settings_.setSplitViewTabsNum(0);
        }

        Launcher launcher(window);
        launcher.openInNewTab();
        launcher.launchPaths(nullptr, pathList);
    }
    else {
        Launcher(nullptr).launchPaths(nullptr, pathList);
    }

    if (openingLastTabs_) {
        openingLastTabs_ = false;

        // if none of the last tabs could be opened and there is still no main window,
        // fall back to opening the current directory
        if (!hasAnyMainWindow(topLevelWidgets())) {
            QStringList fallbackPaths;
            fallbackPaths.push_back(QDir::currentPath());
            launchFiles(QDir::currentPath(), fallbackPaths, inNewWindow, false);
        }
    }
}

void Application::openFolders(Panel::FileInfoList files) {
    Launcher(nullptr).launchFiles(nullptr, std::move(files));
}

void Application::openFolderInTerminal(Panel::FilePath path) {
    if (!settings_.terminal().isEmpty()) {
        Panel::GErrorPtr err;
        const QByteArray terminalName = settings_.terminal().toUtf8();
        if (!Panel::launchTerminal(terminalName.constData(), path, err)) {
            QMessageBox::critical(nullptr, tr("Error"), err.message());
        }
    }
    else {
        // the terminal command is not configured yet, guide the user to the preferences
        QMessageBox::critical(nullptr, tr("Error"), tr("Terminal emulator is not set"));
        preferences(QStringLiteral("advanced"));
    }
}

void Application::preferences(const QString& page) {
    // open or reuse the preferences dialog and show the requested page
    if (!preferencesDialog_) {
        preferencesDialog_ = new PreferencesDialog(page);
    }
    else {
        preferencesDialog_.data()->selectPage(page);
    }

    preferencesDialog_.data()->show();
    preferencesDialog_.data()->raise();
    preferencesDialog_.data()->activateWindow();
}

/* This method receives a list of file:// URIs from DBus and for each URI opens
 * a tab showing its content
 */
void Application::ShowFolders(const QStringList& uriList, const QString& startupId) {
    Q_UNUSED(startupId);

    if (!uriList.isEmpty()) {
        launchFiles(QDir::currentPath(), uriList, false, false);
    }
}

/* This method receives a list of file:// URIs from DBus and opens windows
 * or tabs for each folder, highlighting all listed items within each
 */
void Application::ShowItems(const QStringList& uriList, const QString& startupId) {
    Q_UNUSED(startupId);

    std::unordered_map<Panel::FilePath, Panel::FilePathList, Panel::FilePathHash> groups;
    Panel::FilePathList folders;  // used only for preserving the original parent order

    for (const auto& u : uriList) {
        const QByteArray utf8 = u.toUtf8();
        if (auto path = Panel::FilePath::fromPathStr(utf8.constData())) {
            if (auto parent = path.parent()) {
                auto& paths = groups[parent];
                if (std::find(paths.cbegin(), paths.cend(), path) == paths.cend()) {
                    paths.push_back(std::move(path));
                }
                // remember the order of parent folders
                if (std::find(folders.cbegin(), folders.cend(), parent) == folders.cend()) {
                    folders.push_back(std::move(parent));
                }
            }
        }
    }

    if (groups.empty()) {
        return;
    }

    MainWindow* window = nullptr;

    if (settings_.singleWindowMode()) {
        window = MainWindow::lastActive();

        if (window == nullptr) {
            window = findLastMainWindow(topLevelWidgets());
        }
    }

    if (window == nullptr) {
        window = new MainWindow();
        window->resize(settings_.windowWidth(), settings_.windowHeight());
        if (settings_.windowX() != -1 && settings_.windowY() != -1) {
            window->move(settings_.windowX(), settings_.windowY());
        }
    }

    for (const auto& folder : folders) {
        window->openFolderAndSelectFiles(groups[folder]);
    }

    window->show();
    window->raise();
    window->activateWindow();
}

/* This method receives a list of file:// URIs from DBus and
 * for each valid URI opens a property dialog showing its information
 */
void Application::ShowItemProperties(const QStringList& uriList, const QString& startupId) {
    Q_UNUSED(startupId);

    // resolve URIs into paths and show a properties dialog for each item
    Panel::FilePathList paths;
    for (const auto& u : uriList) {
        const QByteArray utf8 = u.toUtf8();
        Panel::FilePath path = Panel::FilePath::fromPathStr(utf8.constData());
        if (path) {
            paths.push_back(std::move(path));
        }
    }
    if (paths.empty()) {
        return;
    }

    auto* job = new Panel::FileInfoJob{std::move(paths)};
    job->setAutoDelete(true);
    connect(job, &Panel::FileInfoJob::finished, this, &Application::onPropJobFinished, Qt::BlockingQueuedConnection);
    job->runAsync();
}

void Application::onPropJobFinished() {
    auto* job = qobject_cast<Panel::FileInfoJob*>(sender());
    if (!job) {
        return;
    }

    for (auto file : job->files()) {
        auto qtInfo = std::make_shared<QtFileInfo>(QString::fromUtf8(file->path().toString().get()));
        auto* dialog = new FilePropertiesDialog(qtInfo);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }
}

// called when Settings is changed to update UI
void Application::updateFromSettings() {
    const QWidgetList windows = this->topLevelWidgets();
    for (QWidget* window : windows) {
        if (auto* mainWindow = qobject_cast<MainWindow*>(window)) {
            mainWindow->updateFromSettings(settings_);
        }
    }
}

void Application::editBookmarks() {
    if (!editBookmarksialog_) {
        editBookmarksialog_ = new Panel::EditBookmarksDialog(Panel::Bookmarks::globalInstance());
    }
    editBookmarksialog_.data()->show();
}

}  // namespace Oneg4FM
