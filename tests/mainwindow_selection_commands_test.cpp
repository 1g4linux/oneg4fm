/*
 * tests/mainwindow_selection_commands_test.cpp
 */

#include <QTest>

#include "../oneg4fm/mainwindow_selection_commands.h"

namespace {

class FakeSelectionContext final : public Oneg4FM::MainWindowSelectionCommands::Context {
   public:
    bool hasPage = false;
    bool hasSinglePath = false;
    bool hasAccessible = false;
    bool hasDeletable = false;
    bool canPaste = false;
    int selectAllCalls = 0;
    int deselectAllCalls = 0;
    int invertSelectionCalls = 0;
    int copyCalls = 0;
    int cutCalls = 0;
    int pasteCalls = 0;
    int copySelectedPathCalls = 0;

    bool hasCurrentPage() const override { return hasPage; }

    bool hasSingleSelectedPath() const override { return hasSinglePath; }

    bool hasAccessibleSelection() const override { return hasAccessible; }

    bool hasDeletableSelection() const override { return hasDeletable; }

    bool canPasteIntoCurrentFolder() const override { return canPaste; }

    void selectAllFiles() override { ++selectAllCalls; }

    void deselectAllFiles() override { ++deselectAllCalls; }

    void invertFileSelection() override { ++invertSelectionCalls; }

    void copySelectionToClipboard() override { ++copyCalls; }

    void cutSelectionToClipboard() override { ++cutCalls; }

    void pasteClipboardIntoCurrentFolder() override { ++pasteCalls; }

    void copySelectedPathToClipboard() override { ++copySelectedPathCalls; }
};

}  // namespace

class MainWindowSelectionCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void selectionCommandsRequireCurrentPage();
    void clipboardCommandsRespectCapabilityGuards();
    void copyFullPathRequiresExactlyOnePath();
};

void MainWindowSelectionCommandsTest::selectionCommandsRequireCurrentPage() {
    FakeSelectionContext context;
    context.hasPage = false;

    const auto ids = {
        Oneg4FM::MainWindowSelectionCommands::Id::SelectAll,
        Oneg4FM::MainWindowSelectionCommands::Id::DeselectAll,
        Oneg4FM::MainWindowSelectionCommands::Id::InvertSelection,
    };

    for (const auto id : ids) {
        QVERIFY(!Oneg4FM::MainWindowSelectionCommands::canExecute(id, context));
        Oneg4FM::MainWindowSelectionCommands::execute(id, context);
    }

    QCOMPARE(context.selectAllCalls, 0);
    QCOMPARE(context.deselectAllCalls, 0);
    QCOMPARE(context.invertSelectionCalls, 0);

    context.hasPage = true;
    for (const auto id : ids) {
        QVERIFY(Oneg4FM::MainWindowSelectionCommands::canExecute(id, context));
        Oneg4FM::MainWindowSelectionCommands::execute(id, context);
    }

    QCOMPARE(context.selectAllCalls, 1);
    QCOMPARE(context.deselectAllCalls, 1);
    QCOMPARE(context.invertSelectionCalls, 1);
}

void MainWindowSelectionCommandsTest::copyFullPathRequiresExactlyOnePath() {
    FakeSelectionContext context;
    context.hasSinglePath = false;

    QVERIFY(!Oneg4FM::MainWindowSelectionCommands::canExecute(Oneg4FM::MainWindowSelectionCommands::Id::CopyFullPath,
                                                              context));
    Oneg4FM::MainWindowSelectionCommands::execute(Oneg4FM::MainWindowSelectionCommands::Id::CopyFullPath, context);
    QCOMPARE(context.copySelectedPathCalls, 0);

    context.hasSinglePath = true;
    QVERIFY(Oneg4FM::MainWindowSelectionCommands::canExecute(Oneg4FM::MainWindowSelectionCommands::Id::CopyFullPath,
                                                             context));
    Oneg4FM::MainWindowSelectionCommands::execute(Oneg4FM::MainWindowSelectionCommands::Id::CopyFullPath, context);
    QCOMPARE(context.copySelectedPathCalls, 1);
}

void MainWindowSelectionCommandsTest::clipboardCommandsRespectCapabilityGuards() {
    using namespace Oneg4FM::MainWindowSelectionCommands;

    FakeSelectionContext context;
    context.hasAccessible = false;
    context.hasDeletable = false;
    context.canPaste = false;

    QVERIFY(!canExecute(Id::Copy, context));
    QVERIFY(!canExecute(Id::Cut, context));
    QVERIFY(!canExecute(Id::Paste, context));
    execute(Id::Copy, context);
    execute(Id::Cut, context);
    execute(Id::Paste, context);
    QCOMPARE(context.copyCalls, 0);
    QCOMPARE(context.cutCalls, 0);
    QCOMPARE(context.pasteCalls, 0);

    context.hasAccessible = true;
    context.hasDeletable = true;
    context.canPaste = true;
    QVERIFY(canExecute(Id::Copy, context));
    QVERIFY(canExecute(Id::Cut, context));
    QVERIFY(canExecute(Id::Paste, context));
    execute(Id::Copy, context);
    execute(Id::Cut, context);
    execute(Id::Paste, context);
    QCOMPARE(context.copyCalls, 1);
    QCOMPARE(context.cutCalls, 1);
    QCOMPARE(context.pasteCalls, 1);
}

QTEST_MAIN(MainWindowSelectionCommandsTest)

#include "mainwindow_selection_commands_test.moc"
