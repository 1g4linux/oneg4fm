/*
 * Main window file operation command dispatcher implementation
 * oneg4fm/mainwindow_fileops_commands.cpp
 */

#include "mainwindow_fileops_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowFileOpsCommands {

namespace {

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const Context& context) const = 0;
    virtual void execute(Context& context) const = 0;
};

class FilePropertiesCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasSelectedFiles(); }

    void execute(Context& context) const override { context.showFileProperties(); }
};

class FolderPropertiesCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.showFolderProperties(); }
};

class DeleteCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasDeletableSelection(); }

    void execute(Context& context) const override { context.deleteSelection(); }
};

class RenameCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.renamableSelectionCount() > 0; }

    void execute(Context& context) const override { context.renameSelection(); }
};

class BulkRenameCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.renamableSelectionCount() > 1; }

    void execute(Context& context) const override { context.bulkRenameSelection(); }
};

const Command& commandForId(Id id) {
    static const FilePropertiesCommand fileProperties;
    static const FolderPropertiesCommand folderProperties;
    static const DeleteCommand remove;
    static const RenameCommand rename;
    static const BulkRenameCommand bulkRename;

    switch (id) {
        case Id::FileProperties:
            return fileProperties;
        case Id::FolderProperties:
            return folderProperties;
        case Id::Delete:
            return remove;
        case Id::Rename:
            return rename;
        case Id::BulkRename:
            return bulkRename;
    }

    Q_UNREACHABLE();
}

}  // namespace

bool canExecute(Id id, const Context& context) {
    return commandForId(id).canExecute(context);
}

void execute(Id id, Context& context) {
    const Command& command = commandForId(id);
    if (!command.canExecute(context)) {
        return;
    }
    command.execute(context);
}

}  // namespace Oneg4FM::MainWindowFileOpsCommands
