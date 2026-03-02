/*
 * Main window bookmark command dispatcher implementation
 * oneg4fm/mainwindow_bookmark_commands.cpp
 */

#include "mainwindow_bookmark_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowBookmarkCommands {

namespace {

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const QString& bookmarkPath, const Context& context) const = 0;
    virtual void execute(const QString& bookmarkPath, Context& context) const = 0;
};

bool hasBookmarkPath(const QString& bookmarkPath) {
    return !bookmarkPath.isEmpty();
}

class OpenInCurrentTabCommand final : public Command {
   public:
    bool canExecute(const QString& bookmarkPath, const Context& context) const override {
        Q_UNUSED(context);
        return hasBookmarkPath(bookmarkPath);
    }

    void execute(const QString& bookmarkPath, Context& context) const override {
        context.openBookmarkInCurrentTab(bookmarkPath);
    }
};

class OpenInNewTabCommand final : public Command {
   public:
    bool canExecute(const QString& bookmarkPath, const Context& context) const override {
        Q_UNUSED(context);
        return hasBookmarkPath(bookmarkPath);
    }

    void execute(const QString& bookmarkPath, Context& context) const override {
        context.openBookmarkInNewTab(bookmarkPath);
    }
};

class OpenInNewWindowCommand final : public Command {
   public:
    bool canExecute(const QString& bookmarkPath, const Context& context) const override {
        Q_UNUSED(context);
        return hasBookmarkPath(bookmarkPath);
    }

    void execute(const QString& bookmarkPath, Context& context) const override {
        context.openBookmarkInNewWindow(bookmarkPath);
    }
};

class OpenInLastActiveWindowCommand final : public Command {
   public:
    bool canExecute(const QString& bookmarkPath, const Context& context) const override {
        Q_UNUSED(context);
        return hasBookmarkPath(bookmarkPath);
    }

    void execute(const QString& bookmarkPath, Context& context) const override {
        context.openBookmarkInLastActiveWindow(bookmarkPath);
    }
};

const Command& commandForId(Id id) {
    static const OpenInCurrentTabCommand openInCurrentTab;
    static const OpenInNewTabCommand openInNewTab;
    static const OpenInNewWindowCommand openInNewWindow;
    static const OpenInLastActiveWindowCommand openInLastActiveWindow;

    switch (id) {
        case Id::OpenInCurrentTab:
            return openInCurrentTab;
        case Id::OpenInNewTab:
            return openInNewTab;
        case Id::OpenInNewWindow:
            return openInNewWindow;
        case Id::OpenInLastActiveWindow:
            return openInLastActiveWindow;
    }

    Q_UNREACHABLE();
}

}  // namespace

bool canExecute(Id id, const QString& bookmarkPath, const Context& context) {
    return commandForId(id).canExecute(bookmarkPath, context);
}

void execute(Id id, const QString& bookmarkPath, Context& context) {
    const Command& command = commandForId(id);
    if (!command.canExecute(bookmarkPath, context)) {
        return;
    }
    command.execute(bookmarkPath, context);
}

}  // namespace Oneg4FM::MainWindowBookmarkCommands
