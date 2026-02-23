/*
 * Unit tests for general utility functions and core logic
 * tests/test_utils.cpp
 */

#include <QObject>
#include <QString>
#include <QTest>

// Include headers for utility functions
#include "settings.h"

using Oneg4FM::FolderSettings;
using Oneg4FM::Settings;

class TestUtils : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void testViewModeToString();
    void testSortOrderToString();
    void testSortColumnToString();
    void testSidePaneModeToString();
    void testStringConversionRoundtrip();
};

void TestUtils::testViewModeToString() {
    // Test view mode to string conversion
    QCOMPARE(QString(Settings::viewModeToString(Panel::FolderView::IconMode)), QString("icon"));
    QCOMPARE(QString(Settings::viewModeToString(Panel::FolderView::CompactMode)), QString("compact"));
    QCOMPARE(QString(Settings::viewModeToString(Panel::FolderView::DetailedListMode)), QString("detailed"));
    QCOMPARE(QString(Settings::viewModeToString(Panel::FolderView::ThumbnailMode)), QString("thumbnail"));
}

void TestUtils::testSortOrderToString() {
    // Test sort order to string conversion
    QCOMPARE(QString(Settings::sortOrderToString(Qt::AscendingOrder)), QString("ascending"));
    QCOMPARE(QString(Settings::sortOrderToString(Qt::DescendingOrder)), QString("descending"));
}

void TestUtils::testSortColumnToString() {
    // Test sort column to string conversion
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileName)), QString("name"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileSize)), QString("size"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileType)), QString("type"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileMTime)), QString("mtime"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileCrTime)), QString("crtime"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileDTime)), QString("dtime"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileOwner)), QString("owner"));
    QCOMPARE(QString(Settings::sortColumnToString(Panel::FolderModel::ColumnFileGroup)), QString("group"));
}

void TestUtils::testSidePaneModeToString() {
    // Test side pane mode to string conversion
    QCOMPARE(QString(Settings::sidePaneModeToString(Panel::SidePane::ModePlaces)), QString("places"));
    QCOMPARE(QString(Settings::sidePaneModeToString(Panel::SidePane::ModeDirTree)), QString("dirtree"));
    QCOMPARE(QString(Settings::sidePaneModeToString(Panel::SidePane::ModeNone)), QString("none"));
}

void TestUtils::testStringConversionRoundtrip() {
    // Test that string conversions are reversible

    // View modes
    for (auto mode : {Panel::FolderView::IconMode, Panel::FolderView::CompactMode, Panel::FolderView::DetailedListMode,
                      Panel::FolderView::ThumbnailMode}) {
        QString str = Settings::viewModeToString(mode);
        Panel::FolderView::ViewMode converted = FolderSettings::viewModeFromString(str);
        QCOMPARE(converted, mode);
    }

    // Sort orders
    for (auto order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
        QString str = Settings::sortOrderToString(order);
        Qt::SortOrder converted = FolderSettings::sortOrderFromString(str);
        QCOMPARE(converted, order);
    }

    // Sort columns
    for (auto column : {Panel::FolderModel::ColumnFileName, Panel::FolderModel::ColumnFileSize,
                        Panel::FolderModel::ColumnFileType, Panel::FolderModel::ColumnFileMTime,
                        Panel::FolderModel::ColumnFileCrTime, Panel::FolderModel::ColumnFileDTime,
                        Panel::FolderModel::ColumnFileOwner, Panel::FolderModel::ColumnFileGroup}) {
        QString str = Settings::sortColumnToString(column);
        Panel::FolderModel::ColumnId converted = FolderSettings::sortColumnFromString(str);
        QCOMPARE(converted, column);
    }

    // Side pane modes
    for (auto mode : {Panel::SidePane::ModePlaces, Panel::SidePane::ModeDirTree, Panel::SidePane::ModeNone}) {
        QString str = Settings::sidePaneModeToString(mode);
        Panel::SidePane::Mode converted = FolderSettings::sidePaneModeFromString(str);
        QCOMPARE(converted, mode);
    }
}

QTEST_MAIN(TestUtils)
#include "test_utils.moc"
