/*
 * Main window command dispatcher implementation
 * oneg4fm/mainwindow_commands.cpp
 */

#include "mainwindow_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowCommands {

namespace {

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const Context& context) const = 0;
    virtual void execute(Context& context) const = 0;
};

class CloseTabCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasActiveTab(); }

    void execute(Context& context) const override { context.closeActiveTab(); }
};

class NewTabCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.openNewTab(); }
};

class NewWindowCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.openNewWindow(); }
};

class CloseWindowCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.closeWindow(); }
};

class PreferencesCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.openPreferences(); }
};

class EditBookmarksCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.editBookmarks(); }
};

class AboutCommand final : public Command {
   public:
    bool canExecute(const Context& /*context*/) const override { return true; }

    void execute(Context& context) const override { context.showAboutDialog(); }
};

const Command& commandForId(Id id) {
    static const NewTabCommand newTab;
    static const NewWindowCommand newWindow;
    static const CloseTabCommand closeTab;
    static const CloseWindowCommand closeWindow;
    static const PreferencesCommand preferences;
    static const EditBookmarksCommand editBookmarks;
    static const AboutCommand about;

    switch (id) {
        case Id::NewTab:
            return newTab;
        case Id::NewWindow:
            return newWindow;
        case Id::CloseTab:
            return closeTab;
        case Id::CloseWindow:
            return closeWindow;
        case Id::Preferences:
            return preferences;
        case Id::EditBookmarks:
            return editBookmarks;
        case Id::About:
            return about;
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

}  // namespace Oneg4FM::MainWindowCommands
