/*
 * tests/mainwindow_fileops_commands_test.cpp
 */

#include <QTest>

#include "../oneg4fm/mainwindow_fileops_commands.h"

namespace {

class FakeFileOpsContext final : public Oneg4FM::MainWindowFileOpsCommands::Context {
   public:
    bool hasPage = false;
    bool hasSelection = false;
    bool hasDeletable = false;
    int renamableCount = 0;

    int showFilePropertiesCalls = 0;
    int showFolderPropertiesCalls = 0;
    int deleteCalls = 0;
    int renameCalls = 0;
    int bulkRenameCalls = 0;

    bool hasCurrentPage() const override { return hasPage; }
    bool hasSelectedFiles() const override { return hasSelection; }
    bool hasDeletableSelection() const override { return hasDeletable; }
    int renamableSelectionCount() const override { return renamableCount; }

    void showFileProperties() override { ++showFilePropertiesCalls; }
    void showFolderProperties() override { ++showFolderPropertiesCalls; }
    void deleteSelection() override { ++deleteCalls; }
    void renameSelection() override { ++renameCalls; }
    void bulkRenameSelection() override { ++bulkRenameCalls; }
};

}  // namespace

class MainWindowFileOpsCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void fileAndFolderPropertiesCommandGuards();
    void deleteCommandGuard();
    void renameCommandGuards();
};

void MainWindowFileOpsCommandsTest::fileAndFolderPropertiesCommandGuards() {
    using namespace Oneg4FM::MainWindowFileOpsCommands;
    FakeFileOpsContext context;

    context.hasSelection = false;
    QVERIFY(!canExecute(Id::FileProperties, context));
    execute(Id::FileProperties, context);
    QCOMPARE(context.showFilePropertiesCalls, 0);

    context.hasSelection = true;
    QVERIFY(canExecute(Id::FileProperties, context));
    execute(Id::FileProperties, context);
    QCOMPARE(context.showFilePropertiesCalls, 1);

    context.hasPage = false;
    QVERIFY(!canExecute(Id::FolderProperties, context));
    execute(Id::FolderProperties, context);
    QCOMPARE(context.showFolderPropertiesCalls, 0);

    context.hasPage = true;
    QVERIFY(canExecute(Id::FolderProperties, context));
    execute(Id::FolderProperties, context);
    QCOMPARE(context.showFolderPropertiesCalls, 1);
}

void MainWindowFileOpsCommandsTest::deleteCommandGuard() {
    using namespace Oneg4FM::MainWindowFileOpsCommands;
    FakeFileOpsContext context;

    context.hasDeletable = false;
    QVERIFY(!canExecute(Id::Delete, context));

    execute(Id::Delete, context);
    QCOMPARE(context.deleteCalls, 0);

    context.hasDeletable = true;
    QVERIFY(canExecute(Id::Delete, context));

    execute(Id::Delete, context);
    QCOMPARE(context.deleteCalls, 1);
}

void MainWindowFileOpsCommandsTest::renameCommandGuards() {
    using namespace Oneg4FM::MainWindowFileOpsCommands;
    FakeFileOpsContext context;

    context.renamableCount = 0;
    QVERIFY(!canExecute(Id::Rename, context));
    QVERIFY(!canExecute(Id::BulkRename, context));
    execute(Id::Rename, context);
    execute(Id::BulkRename, context);
    QCOMPARE(context.renameCalls, 0);
    QCOMPARE(context.bulkRenameCalls, 0);

    context.renamableCount = 1;
    QVERIFY(canExecute(Id::Rename, context));
    QVERIFY(!canExecute(Id::BulkRename, context));
    execute(Id::Rename, context);
    execute(Id::BulkRename, context);
    QCOMPARE(context.renameCalls, 1);
    QCOMPARE(context.bulkRenameCalls, 0);

    context.renamableCount = 2;
    QVERIFY(canExecute(Id::BulkRename, context));
    execute(Id::BulkRename, context);
    QCOMPARE(context.bulkRenameCalls, 1);
}

QTEST_MAIN(MainWindowFileOpsCommandsTest)

#include "mainwindow_fileops_commands_test.moc"
