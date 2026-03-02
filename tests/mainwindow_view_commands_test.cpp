/*
 * tests/mainwindow_view_commands_test.cpp
 */

#include <QTest>

#include "../oneg4fm/mainwindow_view_commands.h"

namespace {

class FakeViewContext final : public Oneg4FM::MainWindowViewCommands::Context {
   public:
    bool hasPage = false;
    int setIconModeCalls = 0;
    int setCompactModeCalls = 0;
    int setDetailedModeCalls = 0;
    int setThumbnailModeCalls = 0;
    int sortAscendingCalls = 0;
    int sortDescendingCalls = 0;
    int sortByFileNameCalls = 0;
    int sortByMTimeCalls = 0;
    int sortByCrTimeCalls = 0;
    int sortByDTimeCalls = 0;
    int sortByOwnerCalls = 0;
    int sortByGroupCalls = 0;
    int sortByFileSizeCalls = 0;
    int sortByFileTypeCalls = 0;

    bool hasCurrentPage() const override { return hasPage; }

    void setIconMode() override { ++setIconModeCalls; }

    void setCompactMode() override { ++setCompactModeCalls; }

    void setDetailedMode() override { ++setDetailedModeCalls; }

    void setThumbnailMode() override { ++setThumbnailModeCalls; }

    void sortAscending() override { ++sortAscendingCalls; }

    void sortDescending() override { ++sortDescendingCalls; }

    void sortByFileName() override { ++sortByFileNameCalls; }

    void sortByMTime() override { ++sortByMTimeCalls; }

    void sortByCrTime() override { ++sortByCrTimeCalls; }

    void sortByDTime() override { ++sortByDTimeCalls; }

    void sortByOwner() override { ++sortByOwnerCalls; }

    void sortByGroup() override { ++sortByGroupCalls; }

    void sortByFileSize() override { ++sortByFileSizeCalls; }

    void sortByFileType() override { ++sortByFileTypeCalls; }
};

}  // namespace

class MainWindowViewCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void commandsRequireCurrentPage();
};

void MainWindowViewCommandsTest::commandsRequireCurrentPage() {
    FakeViewContext context;
    context.hasPage = false;

    const auto ids = {
        Oneg4FM::MainWindowViewCommands::Id::IconView,       Oneg4FM::MainWindowViewCommands::Id::CompactView,
        Oneg4FM::MainWindowViewCommands::Id::DetailedView,   Oneg4FM::MainWindowViewCommands::Id::ThumbnailView,
        Oneg4FM::MainWindowViewCommands::Id::SortAscending,  Oneg4FM::MainWindowViewCommands::Id::SortDescending,
        Oneg4FM::MainWindowViewCommands::Id::SortByFileName, Oneg4FM::MainWindowViewCommands::Id::SortByMTime,
        Oneg4FM::MainWindowViewCommands::Id::SortByCrTime,   Oneg4FM::MainWindowViewCommands::Id::SortByDTime,
        Oneg4FM::MainWindowViewCommands::Id::SortByOwner,    Oneg4FM::MainWindowViewCommands::Id::SortByGroup,
        Oneg4FM::MainWindowViewCommands::Id::SortByFileSize, Oneg4FM::MainWindowViewCommands::Id::SortByFileType,
    };

    for (const auto id : ids) {
        QVERIFY(!Oneg4FM::MainWindowViewCommands::canExecute(id, context));
        Oneg4FM::MainWindowViewCommands::execute(id, context);
    }

    QCOMPARE(context.setIconModeCalls, 0);
    QCOMPARE(context.setCompactModeCalls, 0);
    QCOMPARE(context.setDetailedModeCalls, 0);
    QCOMPARE(context.setThumbnailModeCalls, 0);
    QCOMPARE(context.sortAscendingCalls, 0);
    QCOMPARE(context.sortDescendingCalls, 0);
    QCOMPARE(context.sortByFileNameCalls, 0);
    QCOMPARE(context.sortByMTimeCalls, 0);
    QCOMPARE(context.sortByCrTimeCalls, 0);
    QCOMPARE(context.sortByDTimeCalls, 0);
    QCOMPARE(context.sortByOwnerCalls, 0);
    QCOMPARE(context.sortByGroupCalls, 0);
    QCOMPARE(context.sortByFileSizeCalls, 0);
    QCOMPARE(context.sortByFileTypeCalls, 0);

    context.hasPage = true;
    for (const auto id : ids) {
        QVERIFY(Oneg4FM::MainWindowViewCommands::canExecute(id, context));
        Oneg4FM::MainWindowViewCommands::execute(id, context);
    }

    QCOMPARE(context.setIconModeCalls, 1);
    QCOMPARE(context.setCompactModeCalls, 1);
    QCOMPARE(context.setDetailedModeCalls, 1);
    QCOMPARE(context.setThumbnailModeCalls, 1);
    QCOMPARE(context.sortAscendingCalls, 1);
    QCOMPARE(context.sortDescendingCalls, 1);
    QCOMPARE(context.sortByFileNameCalls, 1);
    QCOMPARE(context.sortByMTimeCalls, 1);
    QCOMPARE(context.sortByCrTimeCalls, 1);
    QCOMPARE(context.sortByDTimeCalls, 1);
    QCOMPARE(context.sortByOwnerCalls, 1);
    QCOMPARE(context.sortByGroupCalls, 1);
    QCOMPARE(context.sortByFileSizeCalls, 1);
    QCOMPARE(context.sortByFileTypeCalls, 1);
}

QTEST_MAIN(MainWindowViewCommandsTest)

#include "mainwindow_view_commands_test.moc"
