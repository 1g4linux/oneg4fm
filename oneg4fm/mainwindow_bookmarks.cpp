/*
 * Description of file
 * oneg4fm/mainwindow_bookmarks.cpp
 */

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <algorithm>

#include "panel/panel.h"

#include "application.h"
#include "mainwindow.h"
#include "mainwindow_ui_constants.h"
#include "settings.h"
#include "tabpage.h"

namespace Oneg4FM {

namespace {

Settings& appSettings() {
    auto* app = qobject_cast<Application*>(qApp);
    Q_ASSERT(app);
    return app->settings();
}

MainWindowBookmarkCommands::Id bookmarkOpenCommand(OpenDirTargetType target) {
    switch (target) {
        case OpenInCurrentTab:
            return MainWindowBookmarkCommands::Id::OpenInCurrentTab;
        case OpenInNewTab:
            return MainWindowBookmarkCommands::Id::OpenInNewTab;
        case OpenInNewWindow:
            return MainWindowBookmarkCommands::Id::OpenInNewWindow;
        case OpenInLastActiveWindow:
            return MainWindowBookmarkCommands::Id::OpenInLastActiveWindow;
    }
    return MainWindowBookmarkCommands::Id::OpenInCurrentTab;
}

Panel::FilePath filePathFromBookmark(const QString& bookmarkPath) {
    const QByteArray encoded = bookmarkPath.toUtf8();
    return Panel::FilePath::fromPathStr(encoded.constData());
}

}  // namespace

void MainWindow::loadBookmarksMenu() {
    // Clear previously inserted dynamic bookmark actions
    auto* menu = ui.menu_Bookmarks;
    if (!menu) {
        return;
    }

    const auto actions = menu->actions();
    for (auto* action : actions) {
        if (!action) {
            continue;
        }

        // identify bookmark actions via a custom property
        if (action->property(UiConstants::kBookmarkActionProperty).toBool()) {
            menu->removeAction(action);
            action->deleteLater();
        }
    }

    if (!bookmarks_) {
        return;
    }

    const auto& items = bookmarks_->items();
    if (items.empty()) {
        return;
    }

    auto* separator = new QAction(menu);
    separator->setSeparator(true);
    separator->setProperty(UiConstants::kBookmarkActionProperty, true);
    menu->addAction(separator);

    for (const auto& item : items) {
        if (!item) {
            continue;
        }

        const auto pathStr = item->path().toString();
        if (!pathStr) {
            continue;
        }

        QString label = item->name();
        if (label.isEmpty()) {
            label = QString::fromUtf8(pathStr.get());
        }

        auto* action = new QAction(label, menu);
        action->setProperty(UiConstants::kBookmarkActionProperty, true);
        action->setData(QString::fromUtf8(pathStr.get()));
        connect(action, &QAction::triggered, this, &MainWindow::onBookmarkActionTriggered);
        menu->addAction(action);
    }
}

void MainWindow::onBookmarksChanged() {
    loadBookmarksMenu();
}

void MainWindow::onBookmarkActionTriggered() {
    const auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    const QString pathStr = action->data().toString();
    MainWindowBookmarkCommands::execute(bookmarkOpenCommand(appSettings().bookmarkOpenMethod()), pathStr, *this);
}

void MainWindow::on_actionAddToBookmarks_triggered() {
    auto* page = currentPage();
    if (!page) {
        return;
    }

    const Panel::FilePath path = page->path();
    if (!path) {
        return;
    }

    if (!bookmarks_) {
        bookmarks_ = Panel::Bookmarks::globalInstance();
    }
    if (!bookmarks_) {
        return;
    }

    const auto& items = bookmarks_->items();
    const bool alreadyBookmarked = std::any_of(
        items.cbegin(), items.cend(),
        [&path](const std::shared_ptr<const Panel::BookmarkItem>& item) { return item && item->path() == path; });
    if (alreadyBookmarked) {
        return;
    }

    QString name;
    const auto& folder = page->folder();
    if (folder && folder->info()) {
        name = folder->info()->displayName();
    }

    if (name.isEmpty()) {
        const auto baseName = path.baseName();
        if (baseName) {
            name = QString::fromUtf8(baseName.get());
        }
    }

    if (name.isEmpty()) {
        const auto pathStr = path.toString();
        name = QString::fromUtf8(pathStr.get());
    }

    bookmarks_->insert(path, name, static_cast<int>(items.size()));
}

void MainWindow::openBookmarkInCurrentTab(const QString& bookmarkPath) {
    const Panel::FilePath path = filePathFromBookmark(bookmarkPath);
    if (!path) {
        return;
    }
    chdir(path);
}

void MainWindow::openBookmarkInNewTab(const QString& bookmarkPath) {
    const Panel::FilePath path = filePathFromBookmark(bookmarkPath);
    if (!path) {
        return;
    }
    addTab(path);
}

void MainWindow::openBookmarkInNewWindow(const QString& bookmarkPath) {
    const Panel::FilePath path = filePathFromBookmark(bookmarkPath);
    if (!path) {
        return;
    }
    auto* win = new MainWindow(path);
    win->show();
}

void MainWindow::openBookmarkInLastActiveWindow(const QString& bookmarkPath) {
    MainWindow* target = MainWindow::lastActive();
    if (!target) {
        openBookmarkInCurrentTab(bookmarkPath);
        return;
    }

    target->openBookmarkInCurrentTab(bookmarkPath);
    if (target != this) {
        target->raise();
        target->activateWindow();
    }
}

}  // namespace Oneg4FM
