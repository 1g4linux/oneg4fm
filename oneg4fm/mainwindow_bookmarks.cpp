/*
 * Description of file
 * oneg4fm/mainwindow_bookmarks.cpp
 */

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <algorithm>
#include <functional>

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

Panel::FilePath filePathFromBookmark(const QString& bookmarkPath) {
    const QByteArray encoded = bookmarkPath.toUtf8();
    return Panel::FilePath::fromPathStr(encoded.constData());
}

class QtBookmarkMenuContext final : public MainWindowBookmarkCommands::MenuContext {
   public:
    QtBookmarkMenuContext(QMenu& menu, QObject& owner, std::function<void(const QString&)> openBookmark)
        : menu_(menu), owner_(owner), openBookmark_(std::move(openBookmark)) {}

    void removeDynamicBookmarkActions() override {
        const auto actions = menu_.actions();
        for (auto* action : actions) {
            if (!action) {
                continue;
            }
            if (action->property(UiConstants::kBookmarkActionProperty).toBool()) {
                menu_.removeAction(action);
                action->deleteLater();
            }
        }
    }

    void addBookmarkSeparator() override {
        auto* separator = new QAction(&menu_);
        separator->setSeparator(true);
        separator->setProperty(UiConstants::kBookmarkActionProperty, true);
        menu_.addAction(separator);
    }

    void addBookmarkAction(const QString& label, const QString& bookmarkPath) override {
        auto* action = new QAction(label, &menu_);
        action->setProperty(UiConstants::kBookmarkActionProperty, true);
        action->setData(bookmarkPath);
        QObject::connect(action, &QAction::triggered, &owner_, [this, bookmarkPath] { openBookmark_(bookmarkPath); });
        menu_.addAction(action);
    }

   private:
    QMenu& menu_;
    QObject& owner_;
    std::function<void(const QString&)> openBookmark_;
};

}  // namespace

void MainWindow::loadBookmarksMenu() {
    auto* menu = ui.menu_Bookmarks;
    if (!menu) {
        return;
    }

    QList<MainWindowBookmarkCommands::MenuEntry> entries;
    if (!bookmarks_) {
        QtBookmarkMenuContext menuContext(*menu, *this, [this](const QString& bookmarkPath) {
            MainWindowBookmarkCommands::execute(
                MainWindowBookmarkCommands::commandIdForPolicy(
                    static_cast<MainWindowBookmarkCommands::OpenTargetPolicy>(appSettings().bookmarkOpenMethod())),
                bookmarkPath, *this);
        });
        MainWindowBookmarkCommands::rebuildMenu(entries, menuContext);
        return;
    }

    const auto& items = bookmarks_->items();
    entries.reserve(static_cast<qsizetype>(items.size()));

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

        entries.append(MainWindowBookmarkCommands::MenuEntry{QString::fromUtf8(pathStr.get()), label});
    }

    QtBookmarkMenuContext menuContext(*menu, *this, [this](const QString& bookmarkPath) {
        MainWindowBookmarkCommands::execute(
            MainWindowBookmarkCommands::commandIdForPolicy(
                static_cast<MainWindowBookmarkCommands::OpenTargetPolicy>(appSettings().bookmarkOpenMethod())),
            bookmarkPath, *this);
    });
    MainWindowBookmarkCommands::rebuildMenu(entries, menuContext);
}

void MainWindow::onBookmarksChanged() {
    loadBookmarksMenu();
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
