/*
 * Unit tests for Settings class functionality
 * tests/test_settings_functionality.cpp
 */

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// Include the settings header
#include "settings.h"

class TestSettingsFunctionality : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void testSettingsInitialization();
    void testSettingsProfileDir();
    void testSettingsStringConversions();
    void testSettingsLoadSave();
    void testSchemaNormalizationConstraints();
    void testFolderSettingsSchemaNormalization();
};

void TestSettingsFunctionality::testSettingsInitialization() {
    // Test that Settings can be constructed
    Oneg4FM::Settings settings;

    // Test default values
    QVERIFY(!settings.singleWindowMode());
    QVERIFY(settings.alwaysShowTabs());
    QVERIFY(settings.showTabClose());
    QVERIFY(settings.rememberWindowSize());
    QVERIFY(settings.isSidePaneVisible());
    QCOMPARE(settings.sidePaneMode(), Panel::SidePane::ModePlaces);
    QVERIFY(settings.showMenuBar());
    QVERIFY(!settings.splitView());

    QCOMPARE(settings.viewMode(), Panel::FolderView::DetailedListMode);
    QVERIFY(!settings.showHidden());
    QCOMPARE(settings.sortOrder(), Qt::AscendingOrder);
    QCOMPARE(settings.sortColumn(), Panel::FolderModel::ColumnFileName);
    QVERIFY(settings.sortFolderFirst());
    QVERIFY(!settings.sortCaseSensitive());
    QVERIFY(!settings.showFilter());
    QVERIFY(settings.pathBarButtons());

    QVERIFY(!settings.singleClick());
    QVERIFY(!settings.quickExec());
    QVERIFY(!settings.selectNewFiles());
    QVERIFY(settings.confirmDelete());
    QVERIFY(!settings.confirmTrash());
    QVERIFY(!settings.noUsbTrash());
    QVERIFY(!settings.useTrash());  // supportTrash_ is false in this build

    QVERIFY(settings.showThumbnails());
    QVERIFY(!settings.siUnit());
    QVERIFY(!settings.backupAsHidden());
    QVERIFY(settings.showFullNames());
    QVERIFY(settings.shadowHidden());
    QVERIFY(!settings.noItemTooltip());
    QVERIFY(settings.scrollPerPixel());

    QVERIFY(!settings.onlyUserTemplates());
    QVERIFY(!settings.templateTypeOnce());
    QVERIFY(!settings.templateRunApp());
    QVERIFY(!settings.openWithDefaultFileManager());
    QVERIFY(!settings.allSticky());

    QVERIFY(!settings.searchNameCaseInsensitive());
    QVERIFY(!settings.searchContentCaseInsensitive());
    QVERIFY(settings.searchNameRegexp());
    QVERIFY(settings.searchContentRegexp());
    QVERIFY(!settings.searchRecursive());
    QVERIFY(!settings.searchhHidden());
    QCOMPARE(settings.maxSearchHistory(), 0);
}

void TestSettingsFunctionality::testSettingsProfileDir() {
    // Test profile directory resolution
    Oneg4FM::Settings settings;

    QString profileDir = settings.profileDir("default");
    QVERIFY(!profileDir.isEmpty());

    // Should contain "oneg4fm" and the profile name
    QVERIFY(profileDir.contains("oneg4fm"));
    QVERIFY(profileDir.contains("default"));

    // Test with fallback
    QString profileDirWithFallback = settings.profileDir("test-profile", true);
    QVERIFY(!profileDirWithFallback.isEmpty());
}

void TestSettingsFunctionality::testSettingsStringConversions() {
    // Test string conversion utilities

    // View mode conversions
    const char* iconMode = Oneg4FM::Settings::viewModeToString(Fm::FolderView::IconMode);
    QCOMPARE(QString(iconMode), QString("icon"));

    const char* compactMode = Oneg4FM::Settings::viewModeToString(Fm::FolderView::CompactMode);
    QCOMPARE(QString(compactMode), QString("compact"));

    const char* detailedMode = Oneg4FM::Settings::viewModeToString(Fm::FolderView::DetailedListMode);
    QCOMPARE(QString(detailedMode), QString("detailed"));

    const char* thumbnailMode = Oneg4FM::Settings::viewModeToString(Fm::FolderView::ThumbnailMode);
    QCOMPARE(QString(thumbnailMode), QString("thumbnail"));

    // Sort order conversions
    const char* ascending = Oneg4FM::Settings::sortOrderToString(Qt::AscendingOrder);
    QCOMPARE(QString(ascending), QString("ascending"));

    const char* descending = Oneg4FM::Settings::sortOrderToString(Qt::DescendingOrder);
    QCOMPARE(QString(descending), QString("descending"));

    // Sort column conversions
    const char* nameColumn = Oneg4FM::Settings::sortColumnToString(Fm::FolderModel::ColumnFileName);
    QCOMPARE(QString(nameColumn), QString("name"));

    const char* sizeColumn = Oneg4FM::Settings::sortColumnToString(Fm::FolderModel::ColumnFileSize);
    QCOMPARE(QString(sizeColumn), QString("size"));

    const char* mtimeColumn = Oneg4FM::Settings::sortColumnToString(Fm::FolderModel::ColumnFileMTime);
    QCOMPARE(QString(mtimeColumn), QString("mtime"));

    // Side pane mode conversions
    const char* placesMode = Oneg4FM::Settings::sidePaneModeToString(Fm::SidePane::ModePlaces);
    QCOMPARE(QString(placesMode), QString("places"));

    const char* dirTreeMode = Oneg4FM::Settings::sidePaneModeToString(Fm::SidePane::ModeDirTree);
    QCOMPARE(QString(dirTreeMode), QString("dirtree"));

    const char* noneMode = Oneg4FM::Settings::sidePaneModeToString(Fm::SidePane::ModeNone);
    QCOMPARE(QString(noneMode), QString("none"));
}

void TestSettingsFunctionality::testSettingsLoadSave() {
    // Create a temporary directory for test settings
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Set up environment to use temporary directory for config
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

    Oneg4FM::Settings settings;

    // Load default profile (creates empty defaults) and then customize values
    QVERIFY(settings.load("test-profile"));

    settings.setSingleWindowMode(true);
    settings.setBookmarkOpenMethod(Oneg4FM::OpenInNewWindow);
    settings.setPreservePermissions(true);
    settings.setAlwaysShowTabs(false);
    settings.setShowTabClose(false);
    settings.setSwitchToNewTab(true);
    settings.setReopenLastTabs(true);
    settings.showSidePane(false);
    settings.setSidePaneMode(Panel::SidePane::ModeDirTree);
    settings.setShowMenuBar(false);
    settings.setSplitView(true);
    settings.setPathBarButtons(false);

    settings.setViewMode(Panel::FolderView::DetailedListMode);
    settings.setShowHidden(true);
    settings.setSortOrder(Qt::DescendingOrder);
    settings.setSortColumn(Panel::FolderModel::ColumnFileSize);
    settings.setSortFolderFirst(false);
    settings.setSortCaseSensitive(true);
    settings.setShowFilter(true);

    settings.setSingleClick(true);
    settings.setConfirmDelete(false);
    settings.setNoUsbTrash(true);
    settings.setConfirmTrash(true);
    settings.setQuickExec(true);
    settings.setSelectNewFiles(true);

    settings.setShowThumbnails(false);
    settings.setSiUnit(true);
    settings.setBackupAsHidden(true);
    settings.setShowFullNames(false);
    settings.setShadowHidden(false);
    settings.setNoItemTooltip(true);
    settings.setScrollPerPixel(false);

    settings.setOnlyUserTemplates(true);
    settings.setTemplateTypeOnce(true);
    settings.setTemplateRunApp(true);

    settings.setOpenWithDefaultFileManager(true);
    settings.setAllSticky(true);

    settings.setSearchNameCaseInsensitive(true);
    settings.setsearchContentCaseInsensitive(true);
    settings.setSearchNameRegexp(false);
    settings.setSearchContentRegexp(false);
    settings.setSearchRecursive(true);
    settings.setSearchhHidden(true);
    settings.setMaxSearchHistory(5);
    settings.addNamePattern(QStringLiteral("*.txt"));
    settings.addContentPattern(QStringLiteral("needle"));

    QVERIFY(settings.save("test-profile"));

    // Verify settings file was created
    QString settingsPath = settings.profileDir("test-profile") + QStringLiteral("/settings.conf");
    QVERIFY(QFile::exists(settingsPath));

    // Test loading the saved settings
    Oneg4FM::Settings loadedSettings;
    QVERIFY(loadedSettings.load("test-profile"));

    // Verify loaded settings match the saved values
    QCOMPARE(loadedSettings.singleWindowMode(), settings.singleWindowMode());
    QCOMPARE(loadedSettings.bookmarkOpenMethod(), settings.bookmarkOpenMethod());
    QCOMPARE(loadedSettings.preservePermissions(), settings.preservePermissions());
    QCOMPARE(loadedSettings.alwaysShowTabs(), settings.alwaysShowTabs());
    QCOMPARE(loadedSettings.showTabClose(), settings.showTabClose());
    QCOMPARE(loadedSettings.switchToNewTab(), settings.switchToNewTab());
    QCOMPARE(loadedSettings.reopenLastTabs(), settings.reopenLastTabs());
    QCOMPARE(loadedSettings.isSidePaneVisible(), settings.isSidePaneVisible());
    QCOMPARE(loadedSettings.sidePaneMode(), settings.sidePaneMode());
    QCOMPARE(loadedSettings.showMenuBar(), settings.showMenuBar());
    QCOMPARE(loadedSettings.splitView(), settings.splitView());
    QCOMPARE(loadedSettings.pathBarButtons(), settings.pathBarButtons());

    QCOMPARE(loadedSettings.viewMode(), settings.viewMode());
    QCOMPARE(loadedSettings.showHidden(), settings.showHidden());
    QCOMPARE(loadedSettings.sortOrder(), settings.sortOrder());
    QCOMPARE(loadedSettings.sortColumn(), settings.sortColumn());
    QCOMPARE(loadedSettings.sortFolderFirst(), settings.sortFolderFirst());
    QCOMPARE(loadedSettings.sortCaseSensitive(), settings.sortCaseSensitive());
    QCOMPARE(loadedSettings.showFilter(), settings.showFilter());

    QCOMPARE(loadedSettings.singleClick(), settings.singleClick());
    QCOMPARE(loadedSettings.confirmDelete(), settings.confirmDelete());
    QCOMPARE(loadedSettings.noUsbTrash(), settings.noUsbTrash());
    QCOMPARE(loadedSettings.confirmTrash(), settings.confirmTrash());
    QCOMPARE(loadedSettings.quickExec(), settings.quickExec());
    QCOMPARE(loadedSettings.selectNewFiles(), settings.selectNewFiles());

    QCOMPARE(loadedSettings.showThumbnails(), settings.showThumbnails());
    QCOMPARE(loadedSettings.siUnit(), settings.siUnit());
    QCOMPARE(loadedSettings.backupAsHidden(), settings.backupAsHidden());
    QCOMPARE(loadedSettings.showFullNames(), settings.showFullNames());
    QCOMPARE(loadedSettings.shadowHidden(), settings.shadowHidden());
    QCOMPARE(loadedSettings.noItemTooltip(), settings.noItemTooltip());
    QCOMPARE(loadedSettings.scrollPerPixel(), settings.scrollPerPixel());

    QCOMPARE(loadedSettings.onlyUserTemplates(), settings.onlyUserTemplates());
    QCOMPARE(loadedSettings.templateTypeOnce(), settings.templateTypeOnce());
    QCOMPARE(loadedSettings.templateRunApp(), settings.templateRunApp());

    QCOMPARE(loadedSettings.openWithDefaultFileManager(), settings.openWithDefaultFileManager());
    QCOMPARE(loadedSettings.allSticky(), settings.allSticky());

    QCOMPARE(loadedSettings.searchNameCaseInsensitive(), settings.searchNameCaseInsensitive());
    QCOMPARE(loadedSettings.searchContentCaseInsensitive(), settings.searchContentCaseInsensitive());
    QCOMPARE(loadedSettings.searchNameRegexp(), settings.searchNameRegexp());
    QCOMPARE(loadedSettings.searchContentRegexp(), settings.searchContentRegexp());
    QCOMPARE(loadedSettings.searchRecursive(), settings.searchRecursive());
    QCOMPARE(loadedSettings.searchhHidden(), settings.searchhHidden());
    QCOMPARE(loadedSettings.maxSearchHistory(), settings.maxSearchHistory());
    QCOMPARE(loadedSettings.namePatterns(), settings.namePatterns());
    QCOMPARE(loadedSettings.contentPatterns(), settings.contentPatterns());
}

void TestSettingsFunctionality::testSchemaNormalizationConstraints() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("schema-normalization-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Behavior]\n"
        "AutoSelectionDelay=-5\n"
        "\n"
        "[FolderView]\n"
        "BigIconSize=5\n"
        "FolderViewCellMargins=@Size(-7 99)\n"
        "\n"
        "[Window]\n"
        "FixedWidth=0\n"
        "SplitViewTabsNum=-3\n"
        "TabPaths=/tmp/a/../b, /tmp/c//d\n"
        "\n"
        "[Search]\n"
        "MaxSearchHistory=999\n");
    settingsFile.close();

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));

    QCOMPARE(settings.autoSelectionDelay(), 0);
    QCOMPARE(settings.bigIconSize(), 32);
    QCOMPARE(settings.folderViewCellMargins(), QSize(0, 48));
    QCOMPARE(settings.fixedWindowWidth(), 1);
    QCOMPARE(settings.splitViewTabsNum(), 0);
    QCOMPARE(settings.tabPaths(), QStringList({QStringLiteral("/tmp/b"), QStringLiteral("/tmp/c/d")}));
    QCOMPARE(settings.maxSearchHistory(), 50);
}

void TestSettingsFunctionality::testFolderSettingsSchemaNormalization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folderPath = tempDir.path() + QStringLiteral("/folder");
    QVERIFY(QDir().mkpath(folderPath));

    QFile folderConfig(folderPath + QStringLiteral("/.directory"));
    QVERIFY(folderConfig.open(QIODevice::WriteOnly | QIODevice::Text));
    folderConfig.write(
        "[File Manager]\n"
        "SortOrder=descending\n"
        "SortColumn=size\n"
        "ViewMode=icon\n"
        "ShowHidden=true\n"
        "SortFolderFirst=false\n"
        "SortCaseSensitive=not-a-bool\n"
        "Recursive=true\n");
    folderConfig.close();

    Oneg4FM::Settings settings;
    const Panel::FilePath path = Panel::FilePath::fromLocalPath(folderPath.toUtf8().constData());
    const Oneg4FM::FolderSettings folderSettings = settings.loadFolderSettings(path);

    QVERIFY(folderSettings.isCustomized());
    QCOMPARE(folderSettings.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(folderSettings.sortColumn(), Panel::FolderModel::ColumnFileSize);
    QCOMPARE(folderSettings.viewMode(), Panel::FolderView::IconMode);
    QCOMPARE(folderSettings.showHidden(), true);
    QCOMPARE(folderSettings.sortFolderFirst(), false);
    QCOMPARE(folderSettings.sortCaseSensitive(), false);
    QCOMPARE(folderSettings.recursive(), true);
}

QTEST_MAIN(TestSettingsFunctionality)
#include "test_settings_functionality.moc"
