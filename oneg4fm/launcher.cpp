/*
 * File launcher implementation for oneg4fm
 * oneg4fm/launcher.cpp
 */

#include "launcher.h"

// LibFM-Qt Headers
#include "panel/panel.h"

// Qt Headers
#include <QApplication>
#include <QFileInfo>

// Local Headers
#include "application.h"
#include "mainwindow.h"

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

Launcher::Launcher(Oneg4FM::MainWindow* mainWindow)
    : Panel::FileLauncher(), mainWindow_(mainWindow), openInNewTab_(false), openWithDefaultFileManager_(false) {
    setQuickExec(appSettings().quickExec());
}

Launcher::~Launcher() = default;

// open a list of folders either in an existing main window, in new tabs, or with the system file manager
bool Launcher::openFolder(GAppLaunchContext* ctx, const Panel::FileInfoList& folderInfos, Panel::GErrorPtr& /*err*/) {
    if (folderInfos.empty()) {
        return false;
    }

    Settings& settings = appSettings();
    MainWindow* mainWindow = mainWindow_;

    auto it = folderInfos.cbegin();
    const auto end = folderInfos.cend();

    // Handle the first folder specifically (it might create the window)
    Panel::FilePath firstPath = (*it)->path();

    if (!mainWindow) {
        // If there is no oneg4fm main window, we may delegate to the system default file manager.
        // This is done when:
        //   1) openWithDefaultFileManager_ is set (e.g. desktop launch)
        //   2) we are not explicitly opening in new tabs
        //   3) the default file manager exists and is not oneg4fm itself
        if (openWithDefaultFileManager_ && !openInNewTab_) {
            auto defaultApp = Panel::GAppInfoPtr{g_app_info_get_default_for_type("inode/directory", FALSE), false};

            // Check if default app exists and is NOT this application
            if (defaultApp && std::strcmp(g_app_info_get_id(defaultApp.get()), "oneg4fm.desktop") != 0) {
                for (const auto& folder : folderInfos) {
                    Panel::FileLauncher::launchWithDefaultApp(folder, ctx);
                }
                return true;
            }
        }

        // Fall back to opening a new oneg4fm main window
        mainWindow = new MainWindow(std::move(firstPath));
        mainWindow->resize(settings.windowWidth(), settings.windowHeight());
        if (settings.windowX() != -1 && settings.windowY() != -1) {
            mainWindow->move(settings.windowX(), settings.windowY());
        }
    }
    else {
        // We already have a main window, either reuse the current tab or open a new one
        if (openInNewTab_) {
            mainWindow->addTab(std::move(firstPath));
        }
        else {
            mainWindow->chdir(std::move(firstPath));
        }
    }

    // Process remaining folders (always open in new tabs in the same window)
    ++it;  // Move past the first one we just handled
    for (; it != end; ++it) {
        mainWindow->addTab((*it)->path());
    }

    mainWindow->show();
    mainWindow->raise();
    mainWindow->activateWindow();

    // Reset the tab flag for subsequent launches
    openInNewTab_ = false;

    return true;
}

}  // namespace Oneg4FM
