/*
 * tests/mainwindow_navigation_commands_test.cpp
 */

#include <QTest>

#include "../oneg4fm/mainwindow_navigation_commands.h"

namespace {

class FakeNavigationContext final : public Oneg4FM::MainWindowNavigationCommands::Context {
   public:
    bool hasPage = false;
    bool hasDesktop = false;
    int navigateUpCalls = 0;
    int navigateBackCalls = 0;
    int navigateForwardCalls = 0;
    int navigateHomeCalls = 0;
    int reloadCurrentCalls = 0;
    int openApplicationsRootCalls = 0;
    int openTrashRootCalls = 0;
    int openDesktopRootCalls = 0;
    int findFilesFromSelectionCalls = 0;
    int openTerminalAtCurrentCalls = 0;

    bool hasCurrentPage() const override { return hasPage; }

    bool hasDesktopPath() const override { return hasDesktop; }

    void navigateUp() override { ++navigateUpCalls; }

    void navigateBack() override { ++navigateBackCalls; }

    void navigateForward() override { ++navigateForwardCalls; }

    void navigateHome() override { ++navigateHomeCalls; }

    void reloadCurrent() override { ++reloadCurrentCalls; }

    void openApplicationsRoot() override { ++openApplicationsRootCalls; }

    void openTrashRoot() override { ++openTrashRootCalls; }

    void openDesktopRoot() override { ++openDesktopRootCalls; }

    void findFilesFromSelection() override { ++findFilesFromSelectionCalls; }

    void openTerminalAtCurrent() override { ++openTerminalAtCurrentCalls; }
};

}  // namespace

class MainWindowNavigationCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void pageScopedCommandsRequireCurrentPage();
    void rootCommandsDispatchWithoutPage();
    void desktopCommandRequiresDesktopPath();
};

void MainWindowNavigationCommandsTest::pageScopedCommandsRequireCurrentPage() {
    FakeNavigationContext context;
    context.hasPage = false;

    const auto ids = {
        Oneg4FM::MainWindowNavigationCommands::Id::GoUp,      Oneg4FM::MainWindowNavigationCommands::Id::GoBack,
        Oneg4FM::MainWindowNavigationCommands::Id::GoForward, Oneg4FM::MainWindowNavigationCommands::Id::Reload,
        Oneg4FM::MainWindowNavigationCommands::Id::FindFiles, Oneg4FM::MainWindowNavigationCommands::Id::OpenTerminal,
    };

    for (const auto id : ids) {
        QVERIFY(!Oneg4FM::MainWindowNavigationCommands::canExecute(id, context));
        Oneg4FM::MainWindowNavigationCommands::execute(id, context);
    }

    QCOMPARE(context.navigateUpCalls, 0);
    QCOMPARE(context.navigateBackCalls, 0);
    QCOMPARE(context.navigateForwardCalls, 0);
    QCOMPARE(context.reloadCurrentCalls, 0);
    QCOMPARE(context.findFilesFromSelectionCalls, 0);
    QCOMPARE(context.openTerminalAtCurrentCalls, 0);

    context.hasPage = true;
    for (const auto id : ids) {
        QVERIFY(Oneg4FM::MainWindowNavigationCommands::canExecute(id, context));
        Oneg4FM::MainWindowNavigationCommands::execute(id, context);
    }

    QCOMPARE(context.navigateUpCalls, 1);
    QCOMPARE(context.navigateBackCalls, 1);
    QCOMPARE(context.navigateForwardCalls, 1);
    QCOMPARE(context.reloadCurrentCalls, 1);
    QCOMPARE(context.findFilesFromSelectionCalls, 1);
    QCOMPARE(context.openTerminalAtCurrentCalls, 1);
}

void MainWindowNavigationCommandsTest::rootCommandsDispatchWithoutPage() {
    FakeNavigationContext context;

    const auto ids = {
        Oneg4FM::MainWindowNavigationCommands::Id::Home,
        Oneg4FM::MainWindowNavigationCommands::Id::Applications,
        Oneg4FM::MainWindowNavigationCommands::Id::Trash,
    };

    for (const auto id : ids) {
        QVERIFY(Oneg4FM::MainWindowNavigationCommands::canExecute(id, context));
        Oneg4FM::MainWindowNavigationCommands::execute(id, context);
    }

    QCOMPARE(context.navigateHomeCalls, 1);
    QCOMPARE(context.openApplicationsRootCalls, 1);
    QCOMPARE(context.openTrashRootCalls, 1);
}

void MainWindowNavigationCommandsTest::desktopCommandRequiresDesktopPath() {
    FakeNavigationContext context;
    context.hasDesktop = false;

    QVERIFY(!Oneg4FM::MainWindowNavigationCommands::canExecute(Oneg4FM::MainWindowNavigationCommands::Id::Desktop,
                                                               context));
    Oneg4FM::MainWindowNavigationCommands::execute(Oneg4FM::MainWindowNavigationCommands::Id::Desktop, context);
    QCOMPARE(context.openDesktopRootCalls, 0);

    context.hasDesktop = true;
    QVERIFY(
        Oneg4FM::MainWindowNavigationCommands::canExecute(Oneg4FM::MainWindowNavigationCommands::Id::Desktop, context));
    Oneg4FM::MainWindowNavigationCommands::execute(Oneg4FM::MainWindowNavigationCommands::Id::Desktop, context);
    QCOMPARE(context.openDesktopRootCalls, 1);
}

QTEST_MAIN(MainWindowNavigationCommandsTest)

#include "mainwindow_navigation_commands_test.moc"
