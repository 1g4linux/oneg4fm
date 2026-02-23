/*
 * Description of file
 * oneg4fm/mainwindow_bookmarks.cpp
 */

#include <QAction>
#include <QDir>
#include <QMenu>
#include <algorithm>

#include "panel/panel.h"

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"
#include "mainwindow_ui_constants.h"

namespace Oneg4FM {

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
}

void MainWindow::onBookmarksChanged() {
    loadBookmarksMenu();
}

void MainWindow::onBookmarkActionTriggered() {
    const auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    const QVariant data = action->data();
    if (!data.isValid()) {
        return;
    }

    const QString pathStr = data.toString();
    if (pathStr.isEmpty()) {
        return;
    }

    QDir::setCurrent(pathStr);
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

    auto bookmarks = Panel::Bookmarks::globalInstance();
    if (!bookmarks) {
        return;
    }

    const auto& items = bookmarks->items();
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

    bookmarks->insert(path, name, static_cast<int>(items.size()));
}

}  // namespace Oneg4FM
