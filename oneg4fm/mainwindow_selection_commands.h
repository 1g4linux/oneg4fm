/*
 * Main window selection command dispatcher interface
 * oneg4fm/mainwindow_selection_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_SELECTION_COMMANDS_H
#define ONEG4FM_MAINWINDOW_SELECTION_COMMANDS_H

namespace Oneg4FM::MainWindowSelectionCommands {

enum class Id {
    SelectAll,
    DeselectAll,
    InvertSelection,
    CopyFullPath,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual bool hasCurrentPage() const = 0;
    virtual bool hasSingleSelectedPath() const = 0;

    virtual void selectAllFiles() = 0;
    virtual void deselectAllFiles() = 0;
    virtual void invertFileSelection() = 0;
    virtual void copySelectedPathToClipboard() = 0;
};

bool canExecute(Id id, const Context& context);
void execute(Id id, Context& context);

}  // namespace Oneg4FM::MainWindowSelectionCommands

#endif  // ONEG4FM_MAINWINDOW_SELECTION_COMMANDS_H
