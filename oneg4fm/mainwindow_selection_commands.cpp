/*
 * Main window selection command dispatcher implementation
 * oneg4fm/mainwindow_selection_commands.cpp
 */

#include "mainwindow_selection_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowSelectionCommands {

namespace {

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const Context& context) const = 0;
    virtual void execute(Context& context) const = 0;
};

class SelectAllCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.selectAllFiles(); }
};

class DeselectAllCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.deselectAllFiles(); }
};

class InvertSelectionCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.invertFileSelection(); }
};

class CopyFullPathCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasSingleSelectedPath(); }

    void execute(Context& context) const override { context.copySelectedPathToClipboard(); }
};

const Command& commandForId(Id id) {
    static const SelectAllCommand selectAll;
    static const DeselectAllCommand deselectAll;
    static const InvertSelectionCommand invertSelection;
    static const CopyFullPathCommand copyFullPath;

    switch (id) {
        case Id::SelectAll:
            return selectAll;
        case Id::DeselectAll:
            return deselectAll;
        case Id::InvertSelection:
            return invertSelection;
        case Id::CopyFullPath:
            return copyFullPath;
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

}  // namespace Oneg4FM::MainWindowSelectionCommands
