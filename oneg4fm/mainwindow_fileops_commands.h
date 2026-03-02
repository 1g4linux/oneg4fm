/*
 * Main window file operation command dispatcher interface
 * oneg4fm/mainwindow_fileops_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_FILEOPS_COMMANDS_H
#define ONEG4FM_MAINWINDOW_FILEOPS_COMMANDS_H

namespace Oneg4FM::MainWindowFileOpsCommands {

enum class Id {
    FileProperties,
    FolderProperties,
    Copy,
    Cut,
    Paste,
    Delete,
    Rename,
    BulkRename,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual bool hasCurrentPage() const = 0;
    virtual bool hasSelectedFiles() const = 0;
    virtual bool hasAccessibleSelection() const = 0;
    virtual bool hasDeletableSelection() const = 0;
    virtual bool canPasteIntoCurrentFolder() const = 0;
    virtual int renamableSelectionCount() const = 0;

    virtual void showFileProperties() = 0;
    virtual void showFolderProperties() = 0;
    virtual void copySelectionToClipboard() = 0;
    virtual void cutSelectionToClipboard() = 0;
    virtual void pasteClipboardIntoCurrentFolder() = 0;
    virtual void deleteSelection() = 0;
    virtual void renameSelection() = 0;
    virtual void bulkRenameSelection() = 0;
};

bool canExecute(Id id, const Context& context);
void execute(Id id, Context& context);

}  // namespace Oneg4FM::MainWindowFileOpsCommands

#endif  // ONEG4FM_MAINWINDOW_FILEOPS_COMMANDS_H
