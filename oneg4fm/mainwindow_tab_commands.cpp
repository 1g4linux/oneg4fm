/*
 * Main window tab command dispatcher implementation
 * oneg4fm/mainwindow_tab_commands.cpp
 */

#include "mainwindow_tab_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowTabCommands {

namespace {

bool hasActiveTab(const Context& context) {
    const int count = context.tabCount();
    const int index = context.currentTabIndex();
    return count > 0 && index >= 0 && index < count;
}

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const Context& context) const = 0;
    virtual void execute(Context& context) const = 0;
};

class PreviousTabCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return hasActiveTab(context) && context.tabCount() > 1; }

    void execute(Context& context) const override {
        const int index = context.currentTabIndex();
        const int count = context.tabCount();
        const int previous = (index > 0) ? index - 1 : count - 1;
        context.setCurrentTabIndex(previous);
    }
};

class NextTabCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return hasActiveTab(context) && context.tabCount() > 1; }

    void execute(Context& context) const override {
        const int index = context.currentTabIndex();
        const int count = context.tabCount();
        const int next = (index < count - 1) ? index + 1 : 0;
        context.setCurrentTabIndex(next);
    }
};

class CloseLeftTabsCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override {
        return hasActiveTab(context) && context.currentTabIndex() > 0;
    }

    void execute(Context& context) const override {
        for (int i = context.currentTabIndex() - 1; i >= 0; --i) {
            context.closeTabAt(i);
        }
    }
};

class CloseRightTabsCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override {
        return hasActiveTab(context) && context.currentTabIndex() < (context.tabCount() - 1);
    }

    void execute(Context& context) const override {
        const int current = context.currentTabIndex();
        for (int i = context.tabCount() - 1; i > current; --i) {
            context.closeTabAt(i);
        }
    }
};

class CloseOtherTabsCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override {
        return CloseLeftTabsCommand().canExecute(context) || CloseRightTabsCommand().canExecute(context);
    }

    void execute(Context& context) const override {
        CloseRightTabsCommand().execute(context);
        CloseLeftTabsCommand().execute(context);
    }
};

class FocusCurrentTabViewCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return hasActiveTab(context); }

    void execute(Context& context) const override { context.focusCurrentTabView(); }
};

const Command& commandForId(Id id) {
    static const PreviousTabCommand previousTab;
    static const NextTabCommand nextTab;
    static const CloseLeftTabsCommand closeLeftTabs;
    static const CloseRightTabsCommand closeRightTabs;
    static const CloseOtherTabsCommand closeOtherTabs;
    static const FocusCurrentTabViewCommand focusCurrentTabView;

    switch (id) {
        case Id::PreviousTab:
            return previousTab;
        case Id::NextTab:
            return nextTab;
        case Id::CloseLeftTabs:
            return closeLeftTabs;
        case Id::CloseRightTabs:
            return closeRightTabs;
        case Id::CloseOtherTabs:
            return closeOtherTabs;
        case Id::FocusCurrentTabView:
            return focusCurrentTabView;
    }

    Q_UNREACHABLE();
}

}  // namespace

bool canExecute(Id id, const Context& context) {
    return commandForId(id).canExecute(context);
}

bool canExecute(Id id, const Context& defaultContext, const Context* scopedContext) {
    return canExecute(id, scopedContext ? *scopedContext : defaultContext);
}

void execute(Id id, Context& context) {
    const Command& command = commandForId(id);
    if (!command.canExecute(context)) {
        return;
    }
    command.execute(context);
}

void execute(Id id, Context& defaultContext, Context* scopedContext) {
    execute(id, scopedContext ? *scopedContext : defaultContext);
}

}  // namespace Oneg4FM::MainWindowTabCommands
