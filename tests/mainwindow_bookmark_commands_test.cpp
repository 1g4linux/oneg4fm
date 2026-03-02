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

}  // namespace

class MainWindowBookmarkCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void commandsRequireBookmarkPath();
    void commandDispatchesToRequestedTarget();
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

QTEST_MAIN(MainWindowBookmarkCommandsTest)

#include "mainwindow_bookmark_commands_test.moc"
