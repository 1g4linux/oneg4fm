/*
 * Main window navigation command dispatcher interface
 * oneg4fm/mainwindow_navigation_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_NAVIGATION_COMMANDS_H
#define ONEG4FM_MAINWINDOW_NAVIGATION_COMMANDS_H

namespace Oneg4FM::MainWindowNavigationCommands {

enum class Id {
    GoUp,
    GoBack,
    GoForward,
    Home,
    Reload,
    Applications,
    Trash,
    Desktop,
    FindFiles,
    OpenTerminal,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual bool hasCurrentPage() const = 0;
    virtual bool hasDesktopPath() const = 0;

    virtual void navigateUp() = 0;
    virtual void navigateBack() = 0;
    virtual void navigateForward() = 0;
    virtual void navigateHome() = 0;
    virtual void reloadCurrent() = 0;
    virtual void openApplicationsRoot() = 0;
    virtual void openTrashRoot() = 0;
    virtual void openDesktopRoot() = 0;
    virtual void findFilesFromSelection() = 0;
    virtual void openTerminalAtCurrent() = 0;
};

bool canExecute(Id id, const Context& context);
void execute(Id id, Context& context);

}  // namespace Oneg4FM::MainWindowNavigationCommands

#endif  // ONEG4FM_MAINWINDOW_NAVIGATION_COMMANDS_H
