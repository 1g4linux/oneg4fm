/*
 * tests/mainwindow_bookmark_commands_test.cpp
 */

#include <QList>
#include <QTest>

#include "../oneg4fm/mainwindow_bookmark_commands.h"

namespace {

class FakeBookmarkContext final : public Oneg4FM::MainWindowBookmarkCommands::Context {
   public:
    QList<QString> openInCurrentTabCalls;
    QList<QString> openInNewTabCalls;
    QList<QString> openInNewWindowCalls;
    QList<QString> openInLastActiveWindowCalls;

    void openBookmarkInCurrentTab(const QString& bookmarkPath) override { openInCurrentTabCalls.append(bookmarkPath); }

    void openBookmarkInNewTab(const QString& bookmarkPath) override { openInNewTabCalls.append(bookmarkPath); }

    void openBookmarkInNewWindow(const QString& bookmarkPath) override { openInNewWindowCalls.append(bookmarkPath); }

    void openBookmarkInLastActiveWindow(const QString& bookmarkPath) override {
        openInLastActiveWindowCalls.append(bookmarkPath);
    }
};

class FakeMenuContext final : public Oneg4FM::MainWindowBookmarkCommands::MenuContext {
   public:
    enum class Kind {
        Separator,
        Bookmark,
    };

    struct ActionRecord {
        Kind kind = Kind::Separator;
        QString label;
        QString path;
    };

    int removeDynamicCalls = 0;
    QList<ActionRecord> actions;

    void removeDynamicBookmarkActions() override {
        ++removeDynamicCalls;
        actions.clear();
    }

    void addBookmarkSeparator() override { actions.append(ActionRecord{Kind::Separator, QString(), QString()}); }

    void addBookmarkAction(const QString& label, const QString& bookmarkPath) override {
        actions.append(ActionRecord{Kind::Bookmark, label, bookmarkPath});
    }
};

}  // namespace

class MainWindowBookmarkCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void commandsRequireBookmarkPath();
    void commandDispatchesToRequestedTarget();
    void policyMappingCoversAllTargetsAndFallback();
    void rebuildMenuClearsDynamicActionsWhenEmpty();
    void rebuildMenuAddsSeparatorAndBookmarkActions();
};

void MainWindowBookmarkCommandsTest::commandsRequireBookmarkPath() {
    using namespace Oneg4FM::MainWindowBookmarkCommands;

    FakeBookmarkContext context;
    const auto ids = {
        Id::OpenInCurrentTab,
        Id::OpenInNewTab,
        Id::OpenInNewWindow,
        Id::OpenInLastActiveWindow,
    };

    for (const auto id : ids) {
        QVERIFY(!canExecute(id, QString(), context));
        execute(id, QString(), context);
    }

    QCOMPARE(context.openInCurrentTabCalls.size(), 0);
    QCOMPARE(context.openInNewTabCalls.size(), 0);
    QCOMPARE(context.openInNewWindowCalls.size(), 0);
    QCOMPARE(context.openInLastActiveWindowCalls.size(), 0);
}

void MainWindowBookmarkCommandsTest::commandDispatchesToRequestedTarget() {
    using namespace Oneg4FM::MainWindowBookmarkCommands;

    FakeBookmarkContext context;
    const QString bookmarkPath = QStringLiteral("/tmp/bookmark-target");

    QVERIFY(canExecute(Id::OpenInCurrentTab, bookmarkPath, context));
    execute(Id::OpenInCurrentTab, bookmarkPath, context);
    QCOMPARE(context.openInCurrentTabCalls, QList<QString>({bookmarkPath}));

    QVERIFY(canExecute(Id::OpenInNewTab, bookmarkPath, context));
    execute(Id::OpenInNewTab, bookmarkPath, context);
    QCOMPARE(context.openInNewTabCalls, QList<QString>({bookmarkPath}));

    QVERIFY(canExecute(Id::OpenInNewWindow, bookmarkPath, context));
    execute(Id::OpenInNewWindow, bookmarkPath, context);
    QCOMPARE(context.openInNewWindowCalls, QList<QString>({bookmarkPath}));

    QVERIFY(canExecute(Id::OpenInLastActiveWindow, bookmarkPath, context));
    execute(Id::OpenInLastActiveWindow, bookmarkPath, context);
    QCOMPARE(context.openInLastActiveWindowCalls, QList<QString>({bookmarkPath}));
}

void MainWindowBookmarkCommandsTest::policyMappingCoversAllTargetsAndFallback() {
    using namespace Oneg4FM::MainWindowBookmarkCommands;

    QCOMPARE(commandIdForPolicy(OpenTargetPolicy::CurrentTab), Id::OpenInCurrentTab);
    QCOMPARE(commandIdForPolicy(OpenTargetPolicy::NewTab), Id::OpenInNewTab);
    QCOMPARE(commandIdForPolicy(OpenTargetPolicy::NewWindow), Id::OpenInNewWindow);
    QCOMPARE(commandIdForPolicy(OpenTargetPolicy::LastActiveWindow), Id::OpenInLastActiveWindow);

    const auto invalidPolicy = static_cast<OpenTargetPolicy>(99);
    QCOMPARE(commandIdForPolicy(invalidPolicy), Id::OpenInCurrentTab);
}

void MainWindowBookmarkCommandsTest::rebuildMenuClearsDynamicActionsWhenEmpty() {
    using namespace Oneg4FM::MainWindowBookmarkCommands;

    FakeMenuContext menu;
    menu.actions.append(
        FakeMenuContext::ActionRecord{FakeMenuContext::Kind::Bookmark, QStringLiteral("old"), QStringLiteral("/old")});

    rebuildMenu(QList<MenuEntry>{}, menu);

    QCOMPARE(menu.removeDynamicCalls, 1);
    QCOMPARE(menu.actions.size(), 0);
}

void MainWindowBookmarkCommandsTest::rebuildMenuAddsSeparatorAndBookmarkActions() {
    using namespace Oneg4FM::MainWindowBookmarkCommands;

    FakeMenuContext menu;
    const QList<MenuEntry> entries = {
        MenuEntry{QStringLiteral("/first"), QStringLiteral("First Bookmark")},
        MenuEntry{QString(), QStringLiteral("Should Be Skipped")},
        MenuEntry{QStringLiteral("/second"), QString()},
    };

    rebuildMenu(entries, menu);

    QCOMPARE(menu.removeDynamicCalls, 1);
    QCOMPARE(menu.actions.size(), 3);

    QCOMPARE(menu.actions[0].kind, FakeMenuContext::Kind::Separator);

    QCOMPARE(menu.actions[1].kind, FakeMenuContext::Kind::Bookmark);
    QCOMPARE(menu.actions[1].label, QStringLiteral("First Bookmark"));
    QCOMPARE(menu.actions[1].path, QStringLiteral("/first"));

    QCOMPARE(menu.actions[2].kind, FakeMenuContext::Kind::Bookmark);
    QCOMPARE(menu.actions[2].label, QStringLiteral("/second"));
    QCOMPARE(menu.actions[2].path, QStringLiteral("/second"));
}

QTEST_MAIN(MainWindowBookmarkCommandsTest)

#include "mainwindow_bookmark_commands_test.moc"
