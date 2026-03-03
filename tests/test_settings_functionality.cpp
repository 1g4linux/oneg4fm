/*
 * Unit tests for Settings class functionality
 * tests/test_settings_functionality.cpp
 */

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// Include the settings header
#include "settings.h"

#ifndef PCMANFM_SOURCE_DIR
#error "PCMANFM_SOURCE_DIR must be defined"
#endif

class TestSettingsFunctionality : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void testSettingsInitialization();
    void testSettingsProfileDir();
    void testSettingsStringConversions();
    void testSettingsLoadSave();
    void testProfileLoadSaveRoundtripNormalization();
    void testSchemaNormalizationConstraints();
    void testFolderSettingsSchemaNormalization();
    void testProfileSchemaVersionCompatibility();
    void testDirectorySchemaVersionCompatibility();
    void testProfileSchemaMigrationFromVersionZero();
    void testDirectorySchemaMigrationFromVersionZero();
    void testProfileUnknownKeysPreservedAndReported();
    void testProfileUnknownKeysStrictModeRejectsLoad();
    void testFolderSettingsDirectoryOverridesProfileDefaults();
    void testProfileDuplicateKeysLastWriteWins();
    void testDirectoryDuplicateKeysLastWriteWins();
    void testProfileLoadRejectsOversizedFile();
    void testProfileLoadRecoversFromTempFile();
    void testProfileSaveUsesCanonicalListFormatting();
    void testDefaultGenerationFromTemplateMatchesCanonicalOutput();
    void testProfileSchemaMigrationFixtureV0();
    void testDirectorySchemaMigrationFixtureV0();
    void testProfileLoadFuzzSafetySmoke();

   private:
    QString sourcePath(const QString& relative) const;
    QStringList readFixtureLines(const QString& path) const;
    QByteArray makeDeterministicFuzzPayload(quint32 seed) const;
};

QString TestSettingsFunctionality::sourcePath(const QString& relative) const {
    return QStringLiteral(PCMANFM_SOURCE_DIR) + QLatin1Char('/') + relative;
}

QStringList TestSettingsFunctionality::readFixtureLines(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QStringList lines;
    const QStringList rawLines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString& rawLine : rawLines) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        lines.append(trimmed);
    }
    return lines;
}

QByteArray TestSettingsFunctionality::makeDeterministicFuzzPayload(quint32 seed) const {
    static const QByteArray kAlphabet =
        QByteArrayLiteral("[]=/ _-;#ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    QRandomGenerator rng(seed);
    const int lineCount = 1 + static_cast<int>(rng.bounded(96u));
    QByteArray payload;
    payload.reserve(lineCount * 80);
    for (int line = 0; line < lineCount; ++line) {
        const int lineLength = static_cast<int>(rng.bounded(160u));
        for (int i = 0; i < lineLength; ++i) {
            const int index = static_cast<int>(rng.bounded(static_cast<quint32>(kAlphabet.size())));
            payload.append(kAlphabet.at(index));
        }
        payload.append('\n');
    }
    return payload;
}

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

void TestSettingsFunctionality::testProfileLoadSaveRoundtripNormalization() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("roundtrip-normalization-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString longTerminal = QStringLiteral("term-终端-") + QString(6000, QLatin1Char('x'));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QByteArray payload;
    payload += "; parser should ignore comments and preserve semantic values only\n";
    payload += "\n";
    payload += "[Search]\n";
    payload += "NamePatterns = πρώτο, δεύτερο\n";
    payload += "NamePatterns = τρίτο ,  τέταρτο\n";
    payload += "ContentPatterns = café, 数据\n";
    payload += "MaxSearchHistory = 10\n";
    payload += "\n";
    payload += "[Window]\n";
    payload += "TabPaths = /tmp/a/../b, /tmp/c//d\n";
    payload += "AlwaysShowTabs = false\n";
    payload += "AlwaysShowTabs = true\n";
    payload += "\n";
    payload += "[Meta]\n";
    payload += "schema_version = 1\n";
    payload += "\n";
    payload += "[System]\n";
    payload += "Terminal = ";
    payload += longTerminal.toUtf8();
    payload += "\n";
    QVERIFY(settingsFile.write(payload) == payload.size());
    settingsFile.close();

    Oneg4FM::Settings firstLoad;
    QVERIFY(firstLoad.load(profileName));

    QCOMPARE(firstLoad.alwaysShowTabs(), true);
    QCOMPARE(firstLoad.tabPaths(), QStringList({QStringLiteral("/tmp/b"), QStringLiteral("/tmp/c/d")}));
    QCOMPARE(firstLoad.namePatterns(), QStringList({QStringLiteral("τρίτο"), QStringLiteral("τέταρτο")}));
    QCOMPARE(firstLoad.contentPatterns(), QStringList({QStringLiteral("café"), QStringLiteral("数据")}));
    QCOMPARE(firstLoad.terminal(), longTerminal);

    QVERIFY(firstLoad.save(profileName));

    QFile canonicalFile(settingsPath);
    QVERIFY(canonicalFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString canonicalText = QString::fromUtf8(canonicalFile.readAll());
    QVERIFY2(canonicalText.contains(QStringLiteral("schema_version=1")),
             qPrintable(QStringLiteral("schema_version missing in %1").arg(settingsPath)));
    QVERIFY2(!canonicalText.contains(QLatin1Char('#')) && !canonicalText.contains(QLatin1Char(';')),
             qPrintable(QStringLiteral("canonical save should not preserve comments in %1").arg(settingsPath)));

    Oneg4FM::Settings secondLoad;
    QVERIFY(secondLoad.load(profileName));

    QCOMPARE(secondLoad.alwaysShowTabs(), firstLoad.alwaysShowTabs());
    QCOMPARE(secondLoad.tabPaths(), firstLoad.tabPaths());
    QCOMPARE(secondLoad.namePatterns(), firstLoad.namePatterns());
    QCOMPARE(secondLoad.contentPatterns(), firstLoad.contentPatterns());
    QCOMPARE(secondLoad.terminal(), firstLoad.terminal());
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

void TestSettingsFunctionality::testProfileSchemaVersionCompatibility() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("schema-version-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Meta]\n"
        "schema_version=999\n"
        "\n"
        "[Window]\n"
        "AlwaysShowTabs=false\n");
    settingsFile.close();

    Oneg4FM::Settings settings;
    QVERIFY(!settings.load(profileName));

    settings.setAlwaysShowTabs(false);
    QVERIFY(settings.save(profileName));

    QSettings saved(settingsPath, QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("Meta/schema_version")).toInt(), 1);
}

void TestSettingsFunctionality::testDirectorySchemaVersionCompatibility() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folderPath = tempDir.path() + QStringLiteral("/folder");
    QVERIFY(QDir().mkpath(folderPath));

    QFile folderConfig(folderPath + QStringLiteral("/.directory"));
    QVERIFY(folderConfig.open(QIODevice::WriteOnly | QIODevice::Text));
    folderConfig.write(
        "[File Manager]\n"
        "schema_version=999\n"
        "SortOrder=descending\n"
        "ShowHidden=true\n");
    folderConfig.close();

    Oneg4FM::Settings settings;
    const Panel::FilePath path = Panel::FilePath::fromLocalPath(folderPath.toUtf8().constData());
    const Oneg4FM::FolderSettings loaded = settings.loadFolderSettings(path);

    QVERIFY(!loaded.isCustomized());
    QCOMPARE(loaded.sortOrder(), settings.sortOrder());
    QCOMPARE(loaded.showHidden(), settings.showHidden());

    Oneg4FM::FolderSettings toSave;
    toSave.setCustomized(true);
    toSave.setSortOrder(Qt::DescendingOrder);
    toSave.setShowHidden(true);
    settings.saveFolderSettings(path, toSave);

    QSettings saved(folderPath + QStringLiteral("/.directory"), QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("File Manager/schema_version")).toInt(), 1);
}

void TestSettingsFunctionality::testProfileSchemaMigrationFromVersionZero() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("schema-migration-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Meta]\n"
        "schema_version=0\n"
        "\n"
        "[Search]\n"
        "searchHidden=true\n"
        "\n"
        "[Window]\n"
        "AlwaysShowTabs=false\n");
    settingsFile.close();

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));
    QCOMPARE(settings.searchhHidden(), true);
    QCOMPARE(settings.alwaysShowTabs(), false);

    QVERIFY(settings.save(profileName));
    QSettings saved(settingsPath, QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("Meta/schema_version")).toInt(), 1);
    QCOMPARE(saved.value(QStringLiteral("Search/searchhHidden")).toBool(), true);
}

void TestSettingsFunctionality::testDirectorySchemaMigrationFromVersionZero() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folderPath = tempDir.path() + QStringLiteral("/folder");
    QVERIFY(QDir().mkpath(folderPath));

    QFile folderConfig(folderPath + QStringLiteral("/.directory"));
    QVERIFY(folderConfig.open(QIODevice::WriteOnly | QIODevice::Text));
    folderConfig.write(
        "[File Manager]\n"
        "schema_version=0\n"
        "SortOrder=desc\n"
        "ShowHidden=true\n"
        "SortFolderFirst=false\n"
        "Recursive=true\n");
    folderConfig.close();

    Oneg4FM::Settings settings;
    const Panel::FilePath path = Panel::FilePath::fromLocalPath(folderPath.toUtf8().constData());
    const Oneg4FM::FolderSettings loaded = settings.loadFolderSettings(path);

    QVERIFY(loaded.isCustomized());
    QCOMPARE(loaded.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(loaded.showHidden(), true);
    QCOMPARE(loaded.sortFolderFirst(), false);
    QCOMPARE(loaded.recursive(), true);

    settings.saveFolderSettings(path, loaded);
    QSettings saved(folderPath + QStringLiteral("/.directory"), QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("File Manager/schema_version")).toInt(), 1);
}

void TestSettingsFunctionality::testProfileUnknownKeysPreservedAndReported() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QByteArray oldStrictUnknownKeys = qgetenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS");
    qunsetenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS");

    const QString profileName = QStringLiteral("schema-unknown-keys-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Meta]\n"
        "schema_version=1\n"
        "\n"
        "[Window]\n"
        "AlwaysShowTabs=false\n"
        "\n"
        "[Future]\n"
        "ExperimentalFlag=enabled\n"
        "\n"
        "[Search]\n"
        "unsupportedFutureToken=alpha\n");
    settingsFile.close();

    const QString expectedWarning = QStringLiteral(
                                        "Unknown settings keys in %1: Future/ExperimentalFlag (line 8), "
                                        "Search/unsupportedFutureToken (line 11)")
                                        .arg(settingsPath);
    QTest::ignoreMessage(QtWarningMsg, qPrintable(expectedWarning));

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));
    QCOMPARE(settings.alwaysShowTabs(), false);

    QVERIFY(settings.save(profileName));
    QSettings saved(settingsPath, QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("Future/ExperimentalFlag")).toString(), QStringLiteral("enabled"));
    QCOMPARE(saved.value(QStringLiteral("Search/unsupportedFutureToken")).toString(), QStringLiteral("alpha"));

    if (oldStrictUnknownKeys.isNull()) {
        qunsetenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS");
    }
    else {
        qputenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS", oldStrictUnknownKeys);
    }
}

void TestSettingsFunctionality::testProfileUnknownKeysStrictModeRejectsLoad() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QByteArray oldStrictUnknownKeys = qgetenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS");
    QVERIFY(qputenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS", QByteArrayLiteral("1")));

    const QString profileName = QStringLiteral("schema-unknown-keys-strict-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Meta]\n"
        "schema_version=1\n"
        "\n"
        "[Window]\n"
        "AlwaysShowTabs=false\n"
        "\n"
        "[Future]\n"
        "ExperimentalFlag=enabled\n");
    settingsFile.close();

    const QString expectedWarning =
        QStringLiteral("Unknown settings keys in %1: Future/ExperimentalFlag (line 8)").arg(settingsPath);
    QTest::ignoreMessage(QtWarningMsg, qPrintable(expectedWarning));

    Oneg4FM::Settings settings;
    QVERIFY(!settings.load(profileName));

    if (oldStrictUnknownKeys.isNull()) {
        qunsetenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS");
    }
    else {
        qputenv("ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS", oldStrictUnknownKeys);
    }
}

void TestSettingsFunctionality::testFolderSettingsDirectoryOverridesProfileDefaults() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("folder-precedence-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Meta]\n"
        "schema_version=1\n"
        "\n"
        "[FolderView]\n"
        "SortOrder=descending\n"
        "SortColumn=size\n"
        "Mode=thumbnail\n"
        "ShowHidden=true\n"
        "SortFolderFirst=false\n"
        "SortCaseSensitive=true\n");
    settingsFile.close();

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));
    QCOMPARE(settings.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(settings.sortColumn(), Panel::FolderModel::ColumnFileSize);
    QCOMPARE(settings.viewMode(), Panel::FolderView::ThumbnailMode);
    QCOMPARE(settings.showHidden(), true);
    QCOMPARE(settings.sortFolderFirst(), false);
    QCOMPARE(settings.sortCaseSensitive(), true);

    const QString folderPath = tempDir.path() + QStringLiteral("/folder");
    QVERIFY(QDir().mkpath(folderPath));
    QFile folderConfig(folderPath + QStringLiteral("/.directory"));
    QVERIFY(folderConfig.open(QIODevice::WriteOnly | QIODevice::Text));
    folderConfig.write(
        "[File Manager]\n"
        "schema_version=1\n"
        "SortOrder=ascending\n"
        "Recursive=true\n");
    folderConfig.close();

    const Panel::FilePath path = Panel::FilePath::fromLocalPath(folderPath.toUtf8().constData());
    const Oneg4FM::FolderSettings loaded = settings.loadFolderSettings(path);

    QVERIFY(loaded.isCustomized());
    QCOMPARE(loaded.sortOrder(), Qt::AscendingOrder);
    QCOMPARE(loaded.sortColumn(), Panel::FolderModel::ColumnFileSize);
    QCOMPARE(loaded.viewMode(), Panel::FolderView::ThumbnailMode);
    QCOMPARE(loaded.showHidden(), true);
    QCOMPARE(loaded.sortFolderFirst(), false);
    QCOMPARE(loaded.sortCaseSensitive(), true);
    QCOMPARE(loaded.recursive(), true);
}

void TestSettingsFunctionality::testProfileDuplicateKeysLastWriteWins() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("duplicate-keys-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(
        "[Meta]\n"
        "schema_version=1\n"
        "\n"
        "[Window]\n"
        "AlwaysShowTabs=false\n"
        "AlwaysShowTabs=true\n"
        "TabPaths=/tmp/first\n"
        "TabPaths=/tmp/final\n");
    settingsFile.close();

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));

    QCOMPARE(settings.alwaysShowTabs(), true);
    QCOMPARE(settings.tabPaths(), QStringList({QStringLiteral("/tmp/final")}));
}

void TestSettingsFunctionality::testDirectoryDuplicateKeysLastWriteWins() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folderPath = tempDir.path() + QStringLiteral("/folder");
    QVERIFY(QDir().mkpath(folderPath));

    QFile folderConfig(folderPath + QStringLiteral("/.directory"));
    QVERIFY(folderConfig.open(QIODevice::WriteOnly | QIODevice::Text));
    folderConfig.write(
        "[File Manager]\n"
        "schema_version=1\n"
        "SortOrder=ascending\n"
        "SortOrder=descending\n"
        "ShowHidden=false\n"
        "ShowHidden=true\n");
    folderConfig.close();

    Oneg4FM::Settings settings;
    const Panel::FilePath path = Panel::FilePath::fromLocalPath(folderPath.toUtf8().constData());
    const Oneg4FM::FolderSettings loaded = settings.loadFolderSettings(path);

    QVERIFY(loaded.isCustomized());
    QCOMPARE(loaded.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(loaded.showHidden(), true);
}

void TestSettingsFunctionality::testProfileLoadRejectsOversizedFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("oversized-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(settingsFile.write("[Meta]\n"
                               "schema_version=1\n"
                               "\n"
                               "[System]\n"
                               "Terminal=") > 0);
    const QByteArray oversizedValue(1024 * 1024 + 128, 'x');
    QVERIFY(settingsFile.write(oversizedValue) == oversizedValue.size());
    QVERIFY(settingsFile.write("\n") > 0);
    settingsFile.close();

    Oneg4FM::Settings settings;
    QVERIFY(!settings.load(profileName));
}

void TestSettingsFunctionality::testProfileLoadRecoversFromTempFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("temp-recovery-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    const QString tempPath = settingsPath + QStringLiteral(".tmp");

    QFile tempFile(tempPath);
    QVERIFY(tempFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(tempFile.write("[Meta]\n"
                           "schema_version=1\n"
                           "\n"
                           "[Window]\n"
                           "AlwaysShowTabs=false\n") > 0);
    tempFile.close();

    QFile::remove(settingsPath);

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));
    QCOMPARE(settings.alwaysShowTabs(), false);
    QVERIFY(QFile::exists(settingsPath));
}

void TestSettingsFunctionality::testProfileSaveUsesCanonicalListFormatting() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(QStringLiteral("canonical-save-profile")));
    settings.setTabPaths(QStringList({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")}));
    settings.setHiddenColumns(QList<int>({4, 1}));
    QVERIFY(settings.save(QStringLiteral("canonical-save-profile")));

    const QString settingsPath =
        settings.profileDir(QStringLiteral("canonical-save-profile")) + QStringLiteral("/settings.conf");
    QFile saved(settingsPath);
    QVERIFY(saved.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(saved.readAll());

    QVERIFY2(text.contains(QStringLiteral("TabPaths=/tmp/a,/tmp/b")),
             qPrintable(QStringLiteral("TabPaths line not canonical: %1").arg(settingsPath)));
    QVERIFY2(text.contains(QStringLiteral("HiddenColumns=1,4")),
             qPrintable(QStringLiteral("HiddenColumns line not canonical: %1").arg(settingsPath)));
    QVERIFY2(
        !text.contains(QStringLiteral("@Variant")),
        qPrintable(QStringLiteral("Serialized settings must not contain Qt @Variant encoding: %1").arg(settingsPath)));
}

void TestSettingsFunctionality::testDefaultGenerationFromTemplateMatchesCanonicalOutput() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("default-generation-profile");
    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));
    QVERIFY(settings.save(profileName));

    const QString generatedPath = settings.profileDir(profileName) + QStringLiteral("/settings.conf");
    const QString templatePath = sourcePath(QStringLiteral("config/oneg4fm/default/settings.conf.in"));
    QVERIFY2(QFile::exists(generatedPath),
             qPrintable(QStringLiteral("Generated settings.conf missing: %1").arg(generatedPath)));
    QVERIFY2(QFile::exists(templatePath), qPrintable(QStringLiteral("Template missing: %1").arg(templatePath)));

    QFile generatedFile(generatedPath);
    QVERIFY(generatedFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray generatedBytes = generatedFile.readAll();

    QFile templateFile(templatePath);
    QVERIFY(templateFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray templateBytes = templateFile.readAll();

    QCOMPARE(generatedBytes, templateBytes);

    QSettings generated(generatedPath, QSettings::IniFormat);
    QCOMPARE(generated.value(QStringLiteral("Meta/schema_version")).toInt(), 1);
}

void TestSettingsFunctionality::testProfileSchemaMigrationFixtureV0() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    const QString profileName = QStringLiteral("fixture-v0-profile");
    const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
    QVERIFY(QDir().mkpath(profileDir));

    const QString fixturePath = sourcePath(QStringLiteral("tests/fixtures/settings/v0/profile/input/settings.conf"));
    const QString expectedActionsPath =
        sourcePath(QStringLiteral("tests/fixtures/settings/v0/profile/expected/migration_actions.txt"));
    const QString settingsPath = profileDir + QStringLiteral("/settings.conf");
    QVERIFY2(QFile::copy(fixturePath, settingsPath),
             qPrintable(QStringLiteral("Failed to copy profile fixture: %1 -> %2").arg(fixturePath, settingsPath)));

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(profileName));
    QCOMPARE(settings.searchhHidden(), true);
    QCOMPARE(settings.alwaysShowTabs(), false);
    QCOMPARE(settings.lastProfileMigrationSourceVersion(), 0);
    QCOMPARE(settings.lastProfileMigrationTargetVersion(), 1);
    QCOMPARE(settings.lastProfileMigrationActions(), readFixtureLines(expectedActionsPath));

    QVERIFY(settings.save(profileName));
    QSettings saved(settingsPath, QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("Meta/schema_version")).toInt(), 1);
    QCOMPARE(saved.value(QStringLiteral("Search/searchhHidden")).toBool(), true);
    QCOMPARE(saved.value(QStringLiteral("Search/searchHidden")).toBool(), true);
}

void TestSettingsFunctionality::testDirectorySchemaMigrationFixtureV0() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folderPath = tempDir.path() + QStringLiteral("/fixture-folder");
    QVERIFY(QDir().mkpath(folderPath));

    const QString fixturePath = sourcePath(QStringLiteral("tests/fixtures/settings/v0/directory/input/.directory"));
    const QString expectedActionsPath =
        sourcePath(QStringLiteral("tests/fixtures/settings/v0/directory/expected/migration_actions.txt"));
    const QString dirConfigPath = folderPath + QStringLiteral("/.directory");
    QVERIFY2(QFile::copy(fixturePath, dirConfigPath),
             qPrintable(QStringLiteral("Failed to copy directory fixture: %1 -> %2").arg(fixturePath, dirConfigPath)));

    Oneg4FM::Settings settings;
    const Panel::FilePath path = Panel::FilePath::fromLocalPath(folderPath.toUtf8().constData());
    const Oneg4FM::FolderSettings loaded = settings.loadFolderSettings(path);

    QVERIFY(loaded.isCustomized());
    QCOMPARE(loaded.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(loaded.showHidden(), true);
    QCOMPARE(loaded.sortFolderFirst(), false);
    QCOMPARE(loaded.recursive(), true);
    QCOMPARE(settings.lastDirectoryMigrationSourceVersion(), 0);
    QCOMPARE(settings.lastDirectoryMigrationTargetVersion(), 1);
    QCOMPARE(settings.lastDirectoryMigrationActions(), readFixtureLines(expectedActionsPath));

    settings.saveFolderSettings(path, loaded);
    QSettings saved(dirConfigPath, QSettings::IniFormat);
    QCOMPARE(saved.value(QStringLiteral("File Manager/schema_version")).toInt(), 1);
    QCOMPARE(saved.value(QStringLiteral("File Manager/SortOrder")).toString(), QStringLiteral("descending"));
}

void TestSettingsFunctionality::testProfileLoadFuzzSafetySmoke() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    QElapsedTimer totalTimer;
    totalTimer.start();

    for (int i = 0; i < 32; ++i) {
        const QString profileName = QStringLiteral("fuzz-profile-%1").arg(i);
        const QString profileDir = tempDir.path() + QStringLiteral("/oneg4fm/") + profileName;
        QVERIFY(QDir().mkpath(profileDir));
        const QString settingsPath = profileDir + QStringLiteral("/settings.conf");

        QFile fuzzFile(settingsPath);
        QVERIFY(fuzzFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray payload = makeDeterministicFuzzPayload(0xC0FFEEu + static_cast<quint32>(i));
        QVERIFY(fuzzFile.write(payload) == payload.size());
        fuzzFile.close();

        QElapsedTimer caseTimer;
        caseTimer.start();
        Oneg4FM::Settings settings;
        const bool loaded = settings.load(profileName);
        QVERIFY2(caseTimer.elapsed() < 1000, "Settings load should stay bounded for malformed random inputs");
        if (loaded) {
            QVERIFY(settings.save(profileName));
        }
    }

    QVERIFY2(totalTimer.elapsed() < 5000, "Fuzz smoke run should remain lightweight");

    const QString lineBoundProfile = QStringLiteral("fuzz-line-bound-profile");
    const QString lineBoundDir = tempDir.path() + QStringLiteral("/oneg4fm/") + lineBoundProfile;
    QVERIFY(QDir().mkpath(lineBoundDir));
    QFile lineBoundFile(lineBoundDir + QStringLiteral("/settings.conf"));
    QVERIFY(lineBoundFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(lineBoundFile.write("[Meta]\n"
                                "schema_version=1\n"
                                "[System]\n"
                                "Terminal=") > 0);
    const QByteArray longLine(17000, 'x');
    QVERIFY(lineBoundFile.write(longLine) == longLine.size());
    QVERIFY(lineBoundFile.write("\n") > 0);
    lineBoundFile.close();
    Oneg4FM::Settings lineBoundSettings;
    QVERIFY(!lineBoundSettings.load(lineBoundProfile));

    const QString countBoundProfile = QStringLiteral("fuzz-line-count-profile");
    const QString countBoundDir = tempDir.path() + QStringLiteral("/oneg4fm/") + countBoundProfile;
    QVERIFY(QDir().mkpath(countBoundDir));
    QFile countBoundFile(countBoundDir + QStringLiteral("/settings.conf"));
    QVERIFY(countBoundFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QByteArray manyLines("[Meta]\n");
    manyLines.append("schema_version=1\n");
    for (int i = 0; i < 17000; ++i) {
        manyLines.append("K=V\n");
    }
    QVERIFY(countBoundFile.write(manyLines) == manyLines.size());
    countBoundFile.close();
    Oneg4FM::Settings countBoundSettings;
    QVERIFY(!countBoundSettings.load(countBoundProfile));
}

QTEST_MAIN(TestSettingsFunctionality)
#include "test_settings_functionality.moc"
