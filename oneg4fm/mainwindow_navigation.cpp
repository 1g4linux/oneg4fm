/*
 * Main window navigation implementation
 * oneg4fm/mainwindow_navigation.cpp
 */

#include "application.h"
#include "mainwindow.h"
#include "mainwindow_view_controller.h"
#include "tabpage.h"

// Qt Headers
#include <QStandardPaths>
#include <QTimer>

namespace Oneg4FM {

void MainWindow::chdir(Panel::FilePath path, ViewFrame* viewFrame) {
    if (!viewFrame) {
        return;
    }

    // Wait until queued events are processed to prevent re-entrant issues
    // during rapid navigation or initialization.
    QTimer::singleShot(0, viewFrame, [this, path, viewFrame] {
        // Double check validity inside the slot execution
        if (TabPage* page = currentPage(viewFrame)) {
            page->chdir(path, true);
            setTabIcon(page);
            MainWindowViewController::handlePageStateChange(*this, viewFrame, true);
        }
    });
}

void MainWindow::on_actionGoUp_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::GoUp, *this);
}

void MainWindow::on_actionGoBack_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::GoBack, *this);
}

void MainWindow::on_actionGoForward_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::GoForward, *this);
}

void MainWindow::on_actionHome_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::Home, *this);
}

void MainWindow::on_actionReload_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::Reload, *this);
}

void MainWindow::on_actionApplications_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::Applications, *this);
}

void MainWindow::on_actionTrash_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::Trash, *this);
}

void MainWindow::on_actionDesktop_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::Desktop, *this);
}

void MainWindow::on_actionFindFiles_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::FindFiles, *this);
}

void MainWindow::on_actionOpenTerminal_triggered() {
    MainWindowNavigationCommands::execute(MainWindowNavigationCommands::Id::OpenTerminal, *this);
}

bool MainWindow::hasCurrentPage() const {
    return activeViewFrame_ && activeViewFrame_->getStackedWidget() &&
           activeViewFrame_->getStackedWidget()->currentWidget() != nullptr;
}

bool MainWindow::hasDesktopPath() const {
    return !QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).isEmpty();
}

void MainWindow::navigateUp() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->up();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::navigateBack() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->backward();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::navigateForward() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->forward();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::navigateHome() {
    chdir(Panel::FilePath::homeDir());
}

void MainWindow::reloadCurrent() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    page->reload();
    MainWindowViewController::handlePageStateChange(*this, activeViewFrame_, false);
}

void MainWindow::openApplicationsRoot() {
    chdir(Panel::FilePath::fromUri("menu://applications/"));
}

void MainWindow::openTrashRoot() {
    chdir(Panel::FilePath::fromUri("trash:///"));
}

void MainWindow::openDesktopRoot() {
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        chdir(Panel::FilePath::fromLocalPath(desktop.toLocal8Bit().constData()));
    }
}

void MainWindow::findFilesFromSelection() {
    auto* app = static_cast<Application*>(qApp);

    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto files = page->selectedFiles();
    QStringList paths;

    if (!files.empty()) {
        for (const auto& file : files) {
            // Use local path if possible, fallback to display name for virtual paths
            // NOTE: This logic assumes findFiles handles display names or paths correctly
            if (file->isDir()) {
                if (file->isNative()) {
                    paths.append(QString::fromStdString(file->path().localPath().get()));
                }
                else {
                    paths.append(QString::fromStdString(file->path().toString().get()));
                }
            }
        }
    }

    if (paths.isEmpty()) {
        paths.append(page->pathName());
    }

    app->findFiles(paths);
}

void MainWindow::openTerminalAtCurrent() {
    if (TabPage* page = currentPage()) {
        static_cast<Application*>(qApp)->openFolderInTerminal(page->path());
    }
}

}  // namespace Oneg4FM
