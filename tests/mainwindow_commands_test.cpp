/*
 * tests/mainwindow_commands_test.cpp
 */

#include <QTest>

#include "../oneg4fm/mainwindow_commands.h"

namespace {

class FakeCommandContext final : public Oneg4FM::MainWindowCommands::Context {
   public:
    bool hasActiveTabValue = false;
    int closeActiveTabCalls = 0;
    int closeWindowCalls = 0;
    int openPreferencesCalls = 0;
    int editBookmarksCalls = 0;
    int showAboutCalls = 0;

    bool hasActiveTab() const override { return hasActiveTabValue; }

    void closeActiveTab() override { ++closeActiveTabCalls; }

    void closeWindow() override { ++closeWindowCalls; }

    void openPreferences() override { ++openPreferencesCalls; }

    void editBookmarks() override { ++editBookmarksCalls; }

    void showAboutDialog() override { ++showAboutCalls; }
};

}  // namespace

class MainWindowCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void closeTabRespectsActiveTabState();
    void closeWindowDispatchesToContext();
    void preferencesDispatchesToContext();
    void editBookmarksDispatchesToContext();
    void aboutDispatchesToContext();
};

void MainWindowCommandsTest::closeTabRespectsActiveTabState() {
    FakeCommandContext context;

    QVERIFY(!Oneg4FM::MainWindowCommands::canExecute(Oneg4FM::MainWindowCommands::Id::CloseTab, context));
    Oneg4FM::MainWindowCommands::execute(Oneg4FM::MainWindowCommands::Id::CloseTab, context);
    QCOMPARE(context.closeActiveTabCalls, 0);

    context.hasActiveTabValue = true;
    QVERIFY(Oneg4FM::MainWindowCommands::canExecute(Oneg4FM::MainWindowCommands::Id::CloseTab, context));
    Oneg4FM::MainWindowCommands::execute(Oneg4FM::MainWindowCommands::Id::CloseTab, context);
    QCOMPARE(context.closeActiveTabCalls, 1);
}

void MainWindowCommandsTest::closeWindowDispatchesToContext() {
    FakeCommandContext context;

    QVERIFY(Oneg4FM::MainWindowCommands::canExecute(Oneg4FM::MainWindowCommands::Id::CloseWindow, context));
    Oneg4FM::MainWindowCommands::execute(Oneg4FM::MainWindowCommands::Id::CloseWindow, context);
    QCOMPARE(context.closeWindowCalls, 1);
}

void MainWindowCommandsTest::preferencesDispatchesToContext() {
    FakeCommandContext context;

    QVERIFY(Oneg4FM::MainWindowCommands::canExecute(Oneg4FM::MainWindowCommands::Id::Preferences, context));
    Oneg4FM::MainWindowCommands::execute(Oneg4FM::MainWindowCommands::Id::Preferences, context);
    QCOMPARE(context.openPreferencesCalls, 1);
}

void MainWindowCommandsTest::editBookmarksDispatchesToContext() {
    FakeCommandContext context;

    QVERIFY(Oneg4FM::MainWindowCommands::canExecute(Oneg4FM::MainWindowCommands::Id::EditBookmarks, context));
    Oneg4FM::MainWindowCommands::execute(Oneg4FM::MainWindowCommands::Id::EditBookmarks, context);
    QCOMPARE(context.editBookmarksCalls, 1);
}

void MainWindowCommandsTest::aboutDispatchesToContext() {
    FakeCommandContext context;

    QVERIFY(Oneg4FM::MainWindowCommands::canExecute(Oneg4FM::MainWindowCommands::Id::About, context));
    Oneg4FM::MainWindowCommands::execute(Oneg4FM::MainWindowCommands::Id::About, context);
    QCOMPARE(context.showAboutCalls, 1);
}

QTEST_MAIN(MainWindowCommandsTest)

#include "mainwindow_commands_test.moc"
