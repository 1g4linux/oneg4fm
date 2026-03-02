/*
 * Main window command dispatcher interface
 * oneg4fm/mainwindow_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_COMMANDS_H
#define ONEG4FM_MAINWINDOW_COMMANDS_H

namespace Oneg4FM::MainWindowCommands {

enum class Id {
    CloseTab,
    CloseWindow,
    Preferences,
    EditBookmarks,
    About,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual bool hasActiveTab() const = 0;
    virtual void closeActiveTab() = 0;
    virtual void closeWindow() = 0;
    virtual void openPreferences() = 0;
    virtual void editBookmarks() = 0;
    virtual void showAboutDialog() = 0;
};

bool canExecute(Id id, const Context& context);
void execute(Id id, Context& context);

}  // namespace Oneg4FM::MainWindowCommands

#endif  // ONEG4FM_MAINWINDOW_COMMANDS_H
