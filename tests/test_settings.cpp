/*
 * Unit tests for Settings utility functions
 * tests/test_settings.cpp
 */

#include <QObject>
#include <QTest>

// Include the settings header to test the utility functions
#include "settings.h"

using namespace Oneg4FM;

class TestSettings : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void testBookmarkOpenMethodFromString();
    void testViewModeFromString();
    void testSortOrderFromString();
    void testSortColumnFromString();
    void testSidePaneModeFromString();
    void testFolderSettings();
};

void TestSettings::testBookmarkOpenMethodFromString() {
    // Test all valid bookmark open methods
    QCOMPARE(FolderSettings::bookmarkOpenMethodFromString("new_tab"), OpenInNewTab);
    QCOMPARE(FolderSettings::bookmarkOpenMethodFromString("new_window"), OpenInNewWindow);
    QCOMPARE(FolderSettings::bookmarkOpenMethodFromString("last_window"), OpenInLastActiveWindow);

    // Test default case
    QCOMPARE(FolderSettings::bookmarkOpenMethodFromString("invalid"), OpenInCurrentTab);
    QCOMPARE(FolderSettings::bookmarkOpenMethodFromString(""), OpenInCurrentTab);
}

void TestSettings::testViewModeFromString() {
    // Test all valid view modes
    QCOMPARE(FolderSettings::viewModeFromString("icon"), Panel::FolderView::IconMode);
    QCOMPARE(FolderSettings::viewModeFromString("compact"), Panel::FolderView::CompactMode);
    QCOMPARE(FolderSettings::viewModeFromString("detailed"), Panel::FolderView::DetailedListMode);
    QCOMPARE(FolderSettings::viewModeFromString("thumbnail"), Panel::FolderView::ThumbnailMode);

    // Test default case
    QCOMPARE(FolderSettings::viewModeFromString("invalid"), Panel::FolderView::DetailedListMode);
    QCOMPARE(FolderSettings::viewModeFromString(""), Panel::FolderView::DetailedListMode);
}

void TestSettings::testSortOrderFromString() {
    // Test sort order parsing
    QCOMPARE(FolderSettings::sortOrderFromString("ascending"), Qt::AscendingOrder);
    QCOMPARE(FolderSettings::sortOrderFromString("descending"), Qt::DescendingOrder);

    // Test default case
    QCOMPARE(FolderSettings::sortOrderFromString("invalid"), Qt::AscendingOrder);
    QCOMPARE(FolderSettings::sortOrderFromString(""), Qt::AscendingOrder);
}

void TestSettings::testSortColumnFromString() {
    // Test sort column parsing
    QCOMPARE(FolderSettings::sortColumnFromString("name"), Panel::FolderModel::ColumnFileName);
    QCOMPARE(FolderSettings::sortColumnFromString("size"), Panel::FolderModel::ColumnFileSize);
    QCOMPARE(FolderSettings::sortColumnFromString("type"), Panel::FolderModel::ColumnFileType);
    QCOMPARE(FolderSettings::sortColumnFromString("mtime"), Panel::FolderModel::ColumnFileMTime);
    QCOMPARE(FolderSettings::sortColumnFromString("crtime"), Panel::FolderModel::ColumnFileCrTime);
    QCOMPARE(FolderSettings::sortColumnFromString("dtime"), Panel::FolderModel::ColumnFileDTime);
    QCOMPARE(FolderSettings::sortColumnFromString("owner"), Panel::FolderModel::ColumnFileOwner);
    QCOMPARE(FolderSettings::sortColumnFromString("group"), Panel::FolderModel::ColumnFileGroup);

    // Test default case
    QCOMPARE(FolderSettings::sortColumnFromString("invalid"), Panel::FolderModel::ColumnFileName);
    QCOMPARE(FolderSettings::sortColumnFromString(""), Panel::FolderModel::ColumnFileName);
}

void TestSettings::testSidePaneModeFromString() {
    // Test side pane mode parsing
    QCOMPARE(FolderSettings::sidePaneModeFromString("places"), Panel::SidePane::ModePlaces);
    QCOMPARE(FolderSettings::sidePaneModeFromString("dirtree"), Panel::SidePane::ModeDirTree);

    // Test default case
    QCOMPARE(FolderSettings::sidePaneModeFromString("invalid"), Panel::SidePane::ModePlaces);
    QCOMPARE(FolderSettings::sidePaneModeFromString(""), Panel::SidePane::ModePlaces);
}

void TestSettings::testFolderSettings() {
    // Test FolderSettings class
    FolderSettings settings;

    // Test default values
    QVERIFY(!settings.isCustomized());
    QCOMPARE(settings.sortOrder(), Qt::AscendingOrder);
    QCOMPARE(settings.sortColumn(), Fm::FolderModel::ColumnFileName);
    QCOMPARE(settings.viewMode(), Fm::FolderView::DetailedListMode);
    QVERIFY(!settings.showHidden());
    QVERIFY(settings.sortFolderFirst());
    QVERIFY(settings.sortCaseSensitive());
    QVERIFY(!settings.recursive());

    // Test setters
    settings.setCustomized(true);
    QVERIFY(settings.isCustomized());

    settings.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(settings.sortOrder(), Qt::DescendingOrder);

    settings.setSortColumn(Fm::FolderModel::ColumnFileSize);
    QCOMPARE(settings.sortColumn(), Fm::FolderModel::ColumnFileSize);

    settings.setViewMode(Fm::FolderView::DetailedListMode);
    QCOMPARE(settings.viewMode(), Fm::FolderView::DetailedListMode);

    settings.setShowHidden(true);
    QVERIFY(settings.showHidden());

    settings.setSortFolderFirst(false);
    QVERIFY(!settings.sortFolderFirst());

    settings.setSortCaseSensitive(false);
    QVERIFY(!settings.sortCaseSensitive());

    settings.setRecursive(true);
    QVERIFY(settings.recursive());
}

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
