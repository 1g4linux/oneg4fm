/*
 * Main window tab command dispatcher interface
 * oneg4fm/mainwindow_tab_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_TAB_COMMANDS_H
#define ONEG4FM_MAINWINDOW_TAB_COMMANDS_H

namespace Oneg4FM::MainWindowTabCommands {

enum class Id {
    PreviousTab,
    NextTab,
    CloseLeftTabs,
    CloseRightTabs,
    CloseOtherTabs,
    FocusCurrentTabView,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual int currentTabIndex() const = 0;
    virtual int tabCount() const = 0;
    virtual void setCurrentTabIndex(int index) = 0;
    virtual void closeTabAt(int index) = 0;
    virtual void focusCurrentTabView() = 0;
};

bool canExecute(Id id, const Context& context);
bool canExecute(Id id, const Context& defaultContext, const Context* scopedContext);
void execute(Id id, Context& context);
void execute(Id id, Context& defaultContext, Context* scopedContext);

}  // namespace Oneg4FM::MainWindowTabCommands

#endif  // ONEG4FM_MAINWINDOW_TAB_COMMANDS_H
