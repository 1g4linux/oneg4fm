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

QString resolvedMenuLabel(const MenuEntry& entry) {
    return entry.label.isEmpty() ? entry.path : entry.label;
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

Id commandIdForPolicy(OpenTargetPolicy policy) {
    switch (policy) {
        case OpenTargetPolicy::CurrentTab:
            return Id::OpenInCurrentTab;
        case OpenTargetPolicy::NewTab:
            return Id::OpenInNewTab;
        case OpenTargetPolicy::NewWindow:
            return Id::OpenInNewWindow;
        case OpenTargetPolicy::LastActiveWindow:
            return Id::OpenInLastActiveWindow;
    }
    return Id::OpenInCurrentTab;
}

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

void rebuildMenu(const QList<MenuEntry>& entries, MenuContext& context) {
    context.removeDynamicBookmarkActions();

    QList<MenuEntry> validEntries;
    validEntries.reserve(entries.size());
    for (const MenuEntry& entry : entries) {
        if (!hasBookmarkPath(entry.path)) {
            continue;
        }
        validEntries.append(MenuEntry{entry.path, resolvedMenuLabel(entry)});
    }

    if (validEntries.isEmpty()) {
        return;
    }

    context.addBookmarkSeparator();
    for (const MenuEntry& entry : validEntries) {
        context.addBookmarkAction(entry.label, entry.path);
    }
}

}  // namespace Oneg4FM::MainWindowBookmarkCommands
