/*
 * tests/mainwindow_view_controller_test.cpp
 */

#include "../oneg4fm/mainwindow_view_controller.h"
#include <QTest>

namespace {

class FakeViewControllerContext final : public Oneg4FM::MainWindowViewController::Context {
   public:
    bool hasPage = true;
    Oneg4FM::ViewFrame* activeFrame = nullptr;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    Oneg4FM::MainWindowViewController::SortColumn sortColumn = Oneg4FM::MainWindowViewController::SortColumn::FileName;

    int applyViewModeCalls = 0;
    Oneg4FM::MainWindowViewController::ViewMode lastViewMode = Oneg4FM::MainWindowViewController::ViewMode::Detailed;

    int applySortCalls = 0;
    Oneg4FM::MainWindowViewController::SortColumn lastSortColumn =
        Oneg4FM::MainWindowViewController::SortColumn::FileName;
    Qt::SortOrder lastSortOrder = Qt::AscendingOrder;

    int applySortCaseSensitiveCalls = 0;
    bool lastSortCaseSensitive = false;

    int applySortFolderFirstCalls = 0;
    bool lastSortFolderFirst = false;

    int setFilterBarsPersistentCalls = 0;
    bool lastFilterBarsPersistent = false;

    int clearFiltersInCurrentWindowCalls = 0;
    int showCurrentFilterBarCalls = 0;

    int syncPathBarForFrameCalls = 0;
    Oneg4FM::ViewFrame* lastSyncedFrame = nullptr;

    int projectCurrentPageUiCalls = 0;
    bool lastProjectSetFocus = false;

    bool hasCurrentPage() const override { return hasPage; }

    Oneg4FM::ViewFrame* activeViewFrame() const override { return activeFrame; }

    void applyViewMode(Oneg4FM::MainWindowViewController::ViewMode mode) override {
        ++applyViewModeCalls;
        lastViewMode = mode;
    }

    void applySort(Oneg4FM::MainWindowViewController::SortColumn column, Qt::SortOrder order) override {
        ++applySortCalls;
        lastSortColumn = column;
        lastSortOrder = order;
    }

    Qt::SortOrder currentSortOrder() const override { return sortOrder; }

    Oneg4FM::MainWindowViewController::SortColumn currentSortColumn() const override { return sortColumn; }

    void applySortCaseSensitive(bool enabled) override {
        ++applySortCaseSensitiveCalls;
        lastSortCaseSensitive = enabled;
    }

    void applySortFolderFirst(bool enabled) override {
        ++applySortFolderFirstCalls;
        lastSortFolderFirst = enabled;
    }

    void setFilterBarsPersistent(bool enabled) override {
        ++setFilterBarsPersistentCalls;
        lastFilterBarsPersistent = enabled;
    }

    void clearFiltersInCurrentWindow() override { ++clearFiltersInCurrentWindowCalls; }

    void showCurrentFilterBar() override { ++showCurrentFilterBarCalls; }

    void syncPathBarForFrame(Oneg4FM::ViewFrame* frame) override {
        ++syncPathBarForFrameCalls;
        lastSyncedFrame = frame;
    }

    void projectCurrentPageUi(bool setFocus) override {
        ++projectCurrentPageUiCalls;
        lastProjectSetFocus = setFocus;
    }
};

}  // namespace

class MainWindowViewControllerTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void viewAndSortTransitionsRequireCurrentPage();
    void filterCommandsRouteToContext();
    void pageStateChangesSyncExpectedSurface();
};

void MainWindowViewControllerTest::viewAndSortTransitionsRequireCurrentPage() {
    FakeViewControllerContext context;
    context.hasPage = false;
    context.sortOrder = Qt::DescendingOrder;
    context.sortColumn = Oneg4FM::MainWindowViewController::SortColumn::FileType;

    Oneg4FM::MainWindowViewController::setViewMode(context, Oneg4FM::MainWindowViewController::ViewMode::Icon);
    Oneg4FM::MainWindowViewController::sortByColumn(context, Oneg4FM::MainWindowViewController::SortColumn::MTime);
    Oneg4FM::MainWindowViewController::sortByOrder(context, Qt::AscendingOrder);
    Oneg4FM::MainWindowViewController::setSortCaseSensitive(context, true);
    Oneg4FM::MainWindowViewController::setSortFolderFirst(context, true);
    Oneg4FM::MainWindowViewController::showFilterBar(context);

    QCOMPARE(context.applyViewModeCalls, 0);
    QCOMPARE(context.applySortCalls, 0);
    QCOMPARE(context.applySortCaseSensitiveCalls, 0);
    QCOMPARE(context.applySortFolderFirstCalls, 0);
    QCOMPARE(context.showCurrentFilterBarCalls, 0);

    context.hasPage = true;
    Oneg4FM::MainWindowViewController::setViewMode(context, Oneg4FM::MainWindowViewController::ViewMode::Thumbnail);
    Oneg4FM::MainWindowViewController::sortByColumn(context, Oneg4FM::MainWindowViewController::SortColumn::Owner);
    Oneg4FM::MainWindowViewController::sortByOrder(context, Qt::AscendingOrder);
    Oneg4FM::MainWindowViewController::setSortCaseSensitive(context, true);
    Oneg4FM::MainWindowViewController::setSortFolderFirst(context, false);
    Oneg4FM::MainWindowViewController::showFilterBar(context);

    QCOMPARE(context.applyViewModeCalls, 1);
    QCOMPARE(context.lastViewMode, Oneg4FM::MainWindowViewController::ViewMode::Thumbnail);

    QCOMPARE(context.applySortCalls, 2);
    QCOMPARE(context.lastSortColumn, Oneg4FM::MainWindowViewController::SortColumn::FileType);
    QCOMPARE(context.lastSortOrder, Qt::AscendingOrder);

    QCOMPARE(context.applySortCaseSensitiveCalls, 1);
    QCOMPARE(context.lastSortCaseSensitive, true);

    QCOMPARE(context.applySortFolderFirstCalls, 1);
    QCOMPARE(context.lastSortFolderFirst, false);

    QCOMPARE(context.showCurrentFilterBarCalls, 1);
}

void MainWindowViewControllerTest::filterCommandsRouteToContext() {
    FakeViewControllerContext context;
    context.hasPage = false;

    Oneg4FM::MainWindowViewController::setFilterBarsPersistent(context, true);
    Oneg4FM::MainWindowViewController::setFilterBarsPersistent(context, false);
    Oneg4FM::MainWindowViewController::clearFilters(context);

    QCOMPARE(context.setFilterBarsPersistentCalls, 2);
    QCOMPARE(context.lastFilterBarsPersistent, false);
    QCOMPARE(context.clearFiltersInCurrentWindowCalls, 1);
}

void MainWindowViewControllerTest::pageStateChangesSyncExpectedSurface() {
    FakeViewControllerContext context;
    auto* activeFrame = reinterpret_cast<Oneg4FM::ViewFrame*>(quintptr(0x1234));
    auto* inactiveFrame = reinterpret_cast<Oneg4FM::ViewFrame*>(quintptr(0x5678));
    context.activeFrame = activeFrame;

    Oneg4FM::MainWindowViewController::handlePageStateChange(context, activeFrame, true);
    QCOMPARE(context.projectCurrentPageUiCalls, 1);
    QCOMPARE(context.lastProjectSetFocus, true);
    QCOMPARE(context.syncPathBarForFrameCalls, 0);

    Oneg4FM::MainWindowViewController::handlePageStateChange(context, inactiveFrame, false);
    QCOMPARE(context.projectCurrentPageUiCalls, 1);
    QCOMPARE(context.syncPathBarForFrameCalls, 1);
    QCOMPARE(context.lastSyncedFrame, inactiveFrame);

    Oneg4FM::MainWindowViewController::handlePageStateChange(context, nullptr, true);
    QCOMPARE(context.projectCurrentPageUiCalls, 1);
    QCOMPARE(context.syncPathBarForFrameCalls, 1);
}

QTEST_MAIN(MainWindowViewControllerTest)

#include "mainwindow_view_controller_test.moc"
