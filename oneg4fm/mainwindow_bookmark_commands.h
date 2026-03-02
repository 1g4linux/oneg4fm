/*
 * Main window bookmark command dispatcher interface
 * oneg4fm/mainwindow_bookmark_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_BOOKMARK_COMMANDS_H
#define ONEG4FM_MAINWINDOW_BOOKMARK_COMMANDS_H

#include <QString>

namespace Oneg4FM::MainWindowBookmarkCommands {

enum class Id {
    OpenInCurrentTab,
    OpenInNewTab,
    OpenInNewWindow,
    OpenInLastActiveWindow,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual void openBookmarkInCurrentTab(const QString& bookmarkPath) = 0;
    virtual void openBookmarkInNewTab(const QString& bookmarkPath) = 0;
    virtual void openBookmarkInNewWindow(const QString& bookmarkPath) = 0;
    virtual void openBookmarkInLastActiveWindow(const QString& bookmarkPath) = 0;
};

bool canExecute(Id id, const QString& bookmarkPath, const Context& context);
void execute(Id id, const QString& bookmarkPath, Context& context);

}  // namespace Oneg4FM::MainWindowBookmarkCommands

#endif  // ONEG4FM_MAINWINDOW_BOOKMARK_COMMANDS_H
