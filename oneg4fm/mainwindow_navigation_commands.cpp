/*
 * Main window navigation command dispatcher implementation
 * oneg4fm/mainwindow_navigation_commands.cpp
 */

#include "mainwindow_navigation_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowNavigationCommands {

namespace {

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const Context& context) const = 0;
    virtual void execute(Context& context) const = 0;
};

class GoUpCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.navigateUp(); }
};

class GoBackCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.navigateBack(); }
};

class GoForwardCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.navigateForward(); }
};

class HomeCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.navigateHome(); }
};

class ReloadCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.reloadCurrent(); }
};

class ApplicationsCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.openApplicationsRoot(); }
};

class TrashCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.openTrashRoot(); }
};

class DesktopCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasDesktopPath(); }

    void execute(Context& context) const override { context.openDesktopRoot(); }
};

class FindFilesCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.findFilesFromSelection(); }
};

class OpenTerminalCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.openTerminalAtCurrent(); }
};

const Command& commandForId(Id id) {
    static const GoUpCommand goUp;
    static const GoBackCommand goBack;
    static const GoForwardCommand goForward;
    static const HomeCommand home;
    static const ReloadCommand reload;
    static const ApplicationsCommand applications;
    static const TrashCommand trash;
    static const DesktopCommand desktop;
    static const FindFilesCommand findFiles;
    static const OpenTerminalCommand openTerminal;

    switch (id) {
        case Id::GoUp:
            return goUp;
        case Id::GoBack:
            return goBack;
        case Id::GoForward:
            return goForward;
        case Id::Home:
            return home;
        case Id::Reload:
            return reload;
        case Id::Applications:
            return applications;
        case Id::Trash:
            return trash;
        case Id::Desktop:
            return desktop;
        case Id::FindFiles:
            return findFiles;
        case Id::OpenTerminal:
            return openTerminal;
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

}  // namespace Oneg4FM::MainWindowNavigationCommands
