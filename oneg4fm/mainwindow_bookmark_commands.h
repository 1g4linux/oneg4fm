/*
 * Main window bookmark command dispatcher interface
 * oneg4fm/mainwindow_bookmark_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_BOOKMARK_COMMANDS_H
#define ONEG4FM_MAINWINDOW_BOOKMARK_COMMANDS_H

#include <QList>
#include <QString>

namespace Oneg4FM::MainWindowBookmarkCommands {

enum class Id {
    OpenInCurrentTab,
    OpenInNewTab,
    OpenInNewWindow,
    OpenInLastActiveWindow,
};

enum class OpenTargetPolicy {
    CurrentTab = 0,
    NewTab = 1,
    NewWindow = 2,
    LastActiveWindow = 3,
};

struct MenuEntry {
    QString path;
    QString label;
};

class Context {
   public:
    virtual ~Context() = default;

    virtual void openBookmarkInCurrentTab(const QString& bookmarkPath) = 0;
    virtual void openBookmarkInNewTab(const QString& bookmarkPath) = 0;
    virtual void openBookmarkInNewWindow(const QString& bookmarkPath) = 0;
    virtual void openBookmarkInLastActiveWindow(const QString& bookmarkPath) = 0;
};

class MenuContext {
   public:
    virtual ~MenuContext() = default;

    virtual void removeDynamicBookmarkActions() = 0;
    virtual void addBookmarkSeparator() = 0;
    virtual void addBookmarkAction(const QString& label, const QString& bookmarkPath) = 0;
};

Id commandIdForPolicy(OpenTargetPolicy policy);
bool canExecute(Id id, const QString& bookmarkPath, const Context& context);
void execute(Id id, const QString& bookmarkPath, Context& context);
void rebuildMenu(const QList<MenuEntry>& entries, MenuContext& context);

}  // namespace Oneg4FM::MainWindowBookmarkCommands

#endif  // ONEG4FM_MAINWINDOW_BOOKMARK_COMMANDS_H
