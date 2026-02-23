/*
 * Main window navigation implementation
 * oneg4fm/mainwindow_navigation.cpp
 */

#include "application.h"
#include "mainwindow.h"
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

            if (viewFrame == activeViewFrame_) {
                updateUIForCurrentPage();
            }
            else {
                // Update background view frames' location bar
                if (auto* pathBar = qobject_cast<Panel::PathBar*>(viewFrame->getTopBar())) {
                    pathBar->setPath(page->path());
                }
                else if (auto* pathEntry = qobject_cast<Panel::PathEdit*>(viewFrame->getTopBar())) {
                    pathEntry->setText(page->pathName());
                }
            }
        }
    });
}

void MainWindow::on_actionGoUp_triggered() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->up();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::on_actionGoBack_triggered() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->backward();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::on_actionGoForward_triggered() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->forward();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::on_actionHome_triggered() {
    chdir(Panel::FilePath::homeDir());
}

void MainWindow::on_actionReload_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    page->reload();

    // In single-view mode, update the main location bar
    if (pathEntry_) {
        pathEntry_->setText(page->pathName());
    }
}

void MainWindow::on_actionApplications_triggered() {
    chdir(Panel::FilePath::fromUri("menu://applications/"));
}

void MainWindow::on_actionTrash_triggered() {
    chdir(Panel::FilePath::fromUri("trash:///"));
}

void MainWindow::on_actionDesktop_triggered() {
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        chdir(Panel::FilePath::fromLocalPath(desktop.toLocal8Bit().constData()));
    }
}

void MainWindow::on_actionFindFiles_triggered() {
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

void MainWindow::on_actionOpenTerminal_triggered() {
    if (TabPage* page = currentPage()) {
        static_cast<Application*>(qApp)->openFolderInTerminal(page->path());
    }
}

}  // namespace Oneg4FM
