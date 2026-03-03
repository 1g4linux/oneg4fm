/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "settings.h"

#include "panel/panel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaType>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <limits>
#include "../src/ui/fsqt.h"

namespace {

constexpr const char* kSchemaPathEnv = "ONEG4FM_SCHEMA_PATH";
constexpr const char* kInstalledSchemaRelativePath = "oneg4fm/default/schema.json";
constexpr const char* kSourceSchemaRelativePath = "config/oneg4fm/schema.json";
constexpr const char* kProfileSchemaSurfaceId = "profile.settings.conf";

struct ProfileSchemaKey {
    QString key;
    QString type;
    QVariant defaultValue;
    QJsonObject constraints;
};

void appendIfUnique(QStringList& values, const QString& candidate) {
    if (!candidate.isEmpty() && !values.contains(candidate)) {
        values.append(candidate);
    }
}

void collectSchemaCandidatesFromAncestors(QStringList& candidates, const QString& startDirPath) {
    if (startDirPath.isEmpty()) {
        return;
    }

    QDir cursor(startDirPath);
    if (!cursor.exists()) {
        return;
    }

    while (true) {
        appendIfUnique(candidates, cursor.filePath(QString::fromUtf8(kSourceSchemaRelativePath)));
        appendIfUnique(candidates,
                       cursor.filePath(QStringLiteral("share/") + QString::fromUtf8(kInstalledSchemaRelativePath)));

        const QString current = cursor.absolutePath();
        const QString parent = QFileInfo(current).dir().absolutePath();
        if (parent == current) {
            break;
        }
        cursor.setPath(parent);
    }
}

QString resolveSchemaPath() {
    QStringList candidates;

    const QByteArray envSchemaPath = qgetenv(kSchemaPathEnv);
    if (!envSchemaPath.isEmpty()) {
        appendIfUnique(candidates, QFileInfo(QString::fromUtf8(envSchemaPath)).absoluteFilePath());
    }

    const QStringList dataCandidates =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QString::fromUtf8(kInstalledSchemaRelativePath),
                                  QStandardPaths::LocateFile);
    for (const QString& path : dataCandidates) {
        appendIfUnique(candidates, path);
    }

    collectSchemaCandidatesFromAncestors(candidates, QCoreApplication::applicationDirPath());
    collectSchemaCandidatesFromAncestors(candidates, QDir::currentPath());

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isReadable()) {
            return info.absoluteFilePath();
        }
    }

    return QString();
}

bool loadProfileSchema(QVector<ProfileSchemaKey>& schemaKeys) {
    schemaKeys.clear();

    const QString schemaPath = resolveSchemaPath();
    if (schemaPath.isEmpty()) {
        return false;
    }

    QFile schemaFile(schemaPath);
    if (!schemaFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument schemaDoc = QJsonDocument::fromJson(schemaFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !schemaDoc.isObject()) {
        return false;
    }

    const QJsonArray surfaces = schemaDoc.object().value(QStringLiteral("surfaces")).toArray();
    for (const QJsonValue& surfaceValue : surfaces) {
        if (!surfaceValue.isObject()) {
            continue;
        }

        const QJsonObject surface = surfaceValue.toObject();
        if (surface.value(QStringLiteral("id")).toString() != QString::fromUtf8(kProfileSchemaSurfaceId)) {
            continue;
        }

        const QJsonArray keys = surface.value(QStringLiteral("keys")).toArray();
        schemaKeys.reserve(keys.size());
        for (const QJsonValue& keyValue : keys) {
            if (!keyValue.isObject()) {
                continue;
            }

            const QJsonObject keyObj = keyValue.toObject();
            ProfileSchemaKey schemaKey;
            schemaKey.key = keyObj.value(QStringLiteral("key")).toString();
            schemaKey.type = keyObj.value(QStringLiteral("type")).toString();
            schemaKey.defaultValue = keyObj.value(QStringLiteral("default")).toVariant();
            schemaKey.constraints = keyObj.value(QStringLiteral("constraints")).toObject();
            if (!schemaKey.key.isEmpty() && !schemaKey.type.isEmpty()) {
                schemaKeys.push_back(std::move(schemaKey));
            }
        }
        return !schemaKeys.isEmpty();
    }

    return false;
}

QHash<QString, QVariant> readIniAst(const QString& filePath) {
    QHash<QString, QVariant> ast;
    QSettings settings(filePath, QSettings::IniFormat);
    for (const QString& key : settings.allKeys()) {
        ast.insert(key, settings.value(key));
    }
    return ast;
}

bool parseBoolValue(const QVariant& rawValue, bool fallbackValue) {
    if (!rawValue.isValid() || rawValue.isNull()) {
        return fallbackValue;
    }

    if (rawValue.metaType().id() == QMetaType::Bool) {
        return rawValue.toBool();
    }

    const QString token = rawValue.toString().trimmed().toLower();
    if (token == QLatin1String("1") || token == QLatin1String("true") || token == QLatin1String("yes") ||
        token == QLatin1String("on")) {
        return true;
    }
    if (token == QLatin1String("0") || token == QLatin1String("false") || token == QLatin1String("no") ||
        token == QLatin1String("off")) {
        return false;
    }

    return fallbackValue;
}

int parseIntValue(const QVariant& rawValue, int fallbackValue) {
    if (!rawValue.isValid() || rawValue.isNull()) {
        return fallbackValue;
    }

    bool ok = false;
    qlonglong parsed = 0;
    const int typeId = rawValue.metaType().id();
    if (typeId == QMetaType::Int || typeId == QMetaType::UInt || typeId == QMetaType::LongLong ||
        typeId == QMetaType::ULongLong || typeId == QMetaType::Short || typeId == QMetaType::UShort ||
        typeId == QMetaType::Char || typeId == QMetaType::SChar || typeId == QMetaType::UChar) {
        parsed = rawValue.toLongLong(&ok);
    }
    else {
        parsed = QLocale::c().toLongLong(rawValue.toString().trimmed(), &ok);
    }

    if (!ok) {
        return fallbackValue;
    }

    if (parsed > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    if (parsed < std::numeric_limits<int>::min()) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(parsed);
}

QString normalizePathToken(const QString& rawPath) {
    const QString trimmed = rawPath.trimmed();
    if (trimmed.startsWith(QLatin1Char('/'))) {
        return QDir::cleanPath(trimmed);
    }
    return trimmed;
}

QSize parseSizeValue(const QVariant& rawValue, const QSize& fallbackSize) {
    if (rawValue.canConvert<QSize>()) {
        const QSize parsed = rawValue.toSize();
        if (parsed.width() != -1 || parsed.height() != -1) {
            return parsed;
        }
    }

    const QString token = rawValue.toString().trimmed();
    static const QRegularExpression kSizePattern(QStringLiteral("^@?Size\\((-?\\d+)\\s+(-?\\d+)\\)$"));
    const QRegularExpressionMatch match = kSizePattern.match(token);
    if (match.hasMatch()) {
        bool okWidth = false;
        bool okHeight = false;
        const int width = match.captured(1).toInt(&okWidth);
        const int height = match.captured(2).toInt(&okHeight);
        if (okWidth && okHeight) {
            return QSize(width, height);
        }
    }

    return fallbackSize;
}

QStringList variantToTokens(const QVariant& rawValue) {
    if (!rawValue.isValid() || rawValue.isNull()) {
        return {};
    }

    if (rawValue.metaType().id() == QMetaType::QStringList) {
        return rawValue.toStringList();
    }

    if (rawValue.metaType().id() == QMetaType::QVariantList) {
        const QVariantList list = rawValue.toList();
        QStringList out;
        out.reserve(list.size());
        for (const QVariant& entry : list) {
            out.append(entry.toString());
        }
        return out;
    }

    const QString asString = rawValue.toString();
    if (asString.isEmpty()) {
        return {};
    }
    if (asString.contains(QLatin1Char(','))) {
        return asString.split(QLatin1Char(','), Qt::KeepEmptyParts);
    }
    return {asString};
}

QVariant normalizeValueBySchema(const ProfileSchemaKey& schemaKey, const QVariant& rawValue) {
    const QVariant sourceValue = rawValue.isValid() ? rawValue : schemaKey.defaultValue;

    if (schemaKey.type == QLatin1String("bool")) {
        const bool fallbackValue = parseBoolValue(schemaKey.defaultValue, false);
        return parseBoolValue(sourceValue, fallbackValue);
    }

    if (schemaKey.type == QLatin1String("int")) {
        int normalized = parseIntValue(sourceValue, parseIntValue(schemaKey.defaultValue, 0));
        if (schemaKey.constraints.contains(QStringLiteral("min"))) {
            normalized = std::max(normalized, schemaKey.constraints.value(QStringLiteral("min")).toInt(normalized));
        }
        if (schemaKey.constraints.contains(QStringLiteral("max"))) {
            normalized = std::min(normalized, schemaKey.constraints.value(QStringLiteral("max")).toInt(normalized));
        }
        if (schemaKey.constraints.contains(QStringLiteral("normalized_to"))) {
            const QJsonArray normalizedTo = schemaKey.constraints.value(QStringLiteral("normalized_to")).toArray();
            if (!normalizedTo.isEmpty()) {
                int constrained = normalizedTo.at(normalizedTo.size() - 1).toInt(normalized);
                for (const QJsonValue& candidateValue : normalizedTo) {
                    const int candidate = candidateValue.toInt(constrained);
                    if (normalized >= candidate) {
                        constrained = candidate;
                        break;
                    }
                }
                normalized = constrained;
            }
        }
        return normalized;
    }

    if (schemaKey.type == QLatin1String("enum")) {
        const QString fallbackValue = schemaKey.defaultValue.toString();
        const QString candidate = sourceValue.toString().trimmed();
        const QJsonArray allowedValues = schemaKey.constraints.value(QStringLiteral("allowed")).toArray();
        for (const QJsonValue& allowedValue : allowedValues) {
            if (candidate == allowedValue.toString()) {
                return candidate;
            }
        }
        return fallbackValue;
    }

    if (schemaKey.type == QLatin1String("list")) {
        const QString elementType = schemaKey.constraints.value(QStringLiteral("element_type")).toString();
        const QStringList rawTokens = variantToTokens(sourceValue);

        if (elementType == QLatin1String("int")) {
            QVariantList ints;
            for (const QString& token : rawTokens) {
                const QString trimmed = token.trimmed();
                if (trimmed.isEmpty()) {
                    continue;
                }
                ints.append(parseIntValue(trimmed, 0));
            }
            return ints;
        }

        QStringList out;
        for (const QString& token : rawTokens) {
            QString normalized = token.trimmed();
            if (normalized.isEmpty()) {
                continue;
            }
            if (elementType == QLatin1String("path")) {
                normalized = normalizePathToken(normalized);
            }
            out.append(normalized);
        }
        return out;
    }

    if (schemaKey.type == QLatin1String("path")) {
        QString normalized = sourceValue.toString().trimmed();
        const int minLength = schemaKey.constraints.value(QStringLiteral("min_length")).toInt(-1);
        if (minLength >= 0 && normalized.size() < minLength) {
            normalized = schemaKey.defaultValue.toString();
        }
        return normalizePathToken(normalized);
    }

    if (schemaKey.constraints.value(QStringLiteral("format")).toString() == QLatin1String("QSize serialized")) {
        const QSize fallbackSize = parseSizeValue(schemaKey.defaultValue, QSize(3, 3));
        return parseSizeValue(sourceValue, fallbackSize).expandedTo(QSize(0, 0)).boundedTo(QSize(48, 48));
    }

    QString normalized = sourceValue.toString().trimmed();
    const int minLength = schemaKey.constraints.value(QStringLiteral("min_length")).toInt(-1);
    if (minLength >= 0 && normalized.size() < minLength) {
        normalized = schemaKey.defaultValue.toString();
    }
    return normalized;
}

QHash<QString, QVariant> normalizeAstBySchema(const QVector<ProfileSchemaKey>& schemaKeys,
                                              const QHash<QString, QVariant>& ast) {
    QHash<QString, QVariant> normalized;
    normalized.reserve(schemaKeys.size());
    for (const ProfileSchemaKey& schemaKey : schemaKeys) {
        normalized.insert(schemaKey.key, normalizeValueBySchema(schemaKey, ast.value(schemaKey.key)));
    }
    return normalized;
}

QString pickFallbackIconTheme(const QString& configuredTheme) {
    if (!configuredTheme.isEmpty()) {
        return configuredTheme;
    }

    QString fallbackTheme = QLatin1String("Papirus-Dark");
    bool foundInstalledTheme = false;
    const QStringList searchPaths = QIcon::themeSearchPaths();
    for (const QString& path : searchPaths) {
        if (QDir(path).exists(fallbackTheme)) {
            foundInstalledTheme = true;
            break;
        }
    }

    if (!foundInstalledTheme) {
        for (const QString& path : searchPaths) {
            QDir dir(path);
            const QStringList themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& theme : themes) {
                if (theme != QLatin1String("hicolor") && theme != QLatin1String("locolor")) {
                    fallbackTheme = theme;
                    foundInstalledTheme = true;
                    break;
                }
            }
            if (foundInstalledTheme) {
                break;
            }
        }
    }

    return fallbackTheme;
}

}  // namespace

namespace Oneg4FM {

inline static const char* bookmarkOpenMethodToString(OpenDirTargetType value);

Settings::Settings()
    : QObject(),
      supportTrash_(false),  // trash disabled in this build
      fallbackIconThemeName_(),
      useFallbackIconTheme_(QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")),
      singleWindowMode_(false),
      bookmarkOpenMethod_(OpenInCurrentTab),
      preservePermissions_(false),
      terminal_(),
      alwaysShowTabs_(true),
      showTabClose_(true),
      switchToNewTab_(false),
      reopenLastTabs_(false),
      splitViewTabsNum_(0),
      rememberWindowSize_(true),
      fixedWindowWidth_(640),
      fixedWindowHeight_(480),
      lastWindowWidth_(640),
      lastWindowHeight_(480),
      lastWindowX_(-1),
      lastWindowY_(-1),
      lastWindowMaximized_(false),
      splitterPos_(120),
      sidePaneVisible_(true),
      sidePaneMode_(Panel::SidePane::ModePlaces),
      showMenuBar_(true),
      splitView_(false),
      viewMode_(Panel::FolderView::DetailedListMode),
      showHidden_(false),
      sortOrder_(Qt::AscendingOrder),
      sortColumn_(Panel::FolderModel::ColumnFileName),
      sortFolderFirst_(true),
      sortCaseSensitive_(false),
      showFilter_(false),
      pathBarButtons_(true),
      // settings for use with libfm
      singleClick_(false),
      autoSelectionDelay_(600),
      ctrlRightClick_(false),
      useTrash_(true),
      confirmDelete_(true),
      noUsbTrash_(false),
      confirmTrash_(false),
      quickExec_(false),
      selectNewFiles_(false),
      showThumbnails_(true),
      archiver_(),
      siUnit_(false),
      backupAsHidden_(false),
      showFullNames_(true),
      shadowHidden_(true),
      noItemTooltip_(false),
      scrollPerPixel_(true),
      bigIconSize_(48),
      smallIconSize_(24),
      sidePaneIconSize_(24),
      thumbnailIconSize_(128),
      onlyUserTemplates_(false),
      templateTypeOnce_(false),
      templateRunApp_(false),
      folderViewCellMargins_(QSize(3, 3)),
      openWithDefaultFileManager_(false),
      allSticky_(false),
      searchNameCaseInsensitive_(false),
      searchContentCaseInsensitive_(false),
      searchNameRegexp_(true),
      searchContentRegexp_(true),
      searchRecursive_(false),
      searchhHidden_(false),
      maxSearchHistory_(0) {}

Settings::~Settings() = default;

QString Settings::xdgUserConfigDir() {
    QString dirName;
    // WARNING: Don't use XDG_CONFIG_HOME with root because it might
    // give the user config directory if gksu-properties is set to su.
    if (geteuid() != 0) {  // non-root user
        dirName = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    if (dirName.isEmpty()) {
        dirName = QDir::homePath() + QLatin1String("/.config");
    }
    return dirName;
}

QString Settings::profileDir(QString profile, bool useFallback) {
    // try user-specific config file first
    QString dirName =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/oneg4fm/") + profile;
    QDir dir(dirName);

    // if user config dir does not exist, try system-wide config dirs instead
    if (!dir.exists() && useFallback) {
        QString fallbackDir;
        const QStringList confList = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation);
        for (const auto& thisConf : confList) {
            fallbackDir = thisConf + QStringLiteral("/oneg4fm/") + profile;
            if (fallbackDir == dirName) {
                continue;
            }
            dir.setPath(fallbackDir);
            if (dir.exists()) {
                dirName = fallbackDir;
                break;
            }
        }
    }
    return dirName;
}

bool Settings::load(QString profile) {
    profileName_ = profile;
    QString fileName = profileDir(profile, true) + QStringLiteral("/settings.conf");
    bool ret = loadFile(fileName);
    return ret;
}

bool Settings::save(QString profile) {
    QString fileName = profileDir(profile.isEmpty() ? profileName_ : profile) + QStringLiteral("/settings.conf");
    bool ret = saveFile(fileName);
    return ret;
}

bool Settings::loadFile(QString filePath) {
    QVector<ProfileSchemaKey> schemaKeys;
    if (!loadProfileSchema(schemaKeys)) {
        return false;
    }

    const QHash<QString, QVariant> normalized = normalizeAstBySchema(schemaKeys, readIniAst(filePath));
    const auto value = [&](const char* key) { return normalized.value(QString::fromUtf8(key)); };

    fallbackIconThemeName_ = pickFallbackIconTheme(value("System/FallbackIconThemeName").toString());
    setTerminal(value("System/Terminal").toString());
    setArchiver(value("System/Archiver").toString());
    setSiUnit(value("System/SIUnit").toBool());
    setOnlyUserTemplates(value("System/OnlyUserTemplates").toBool());
    setTemplateTypeOnce(value("System/TemplateTypeOnce").toBool());
    setTemplateRunApp(value("System/TemplateRunApp").toBool());

    singleWindowMode_ = value("Behavior/SingleWindowMode").toBool();
    bookmarkOpenMethod_ = FolderSettings::bookmarkOpenMethodFromString(value("Behavior/BookmarkOpenMethod").toString());
    preservePermissions_ = value("Behavior/PreservePermissions").toBool();
    // trash support is hard-disabled in this build
    useTrash_ = false;
    singleClick_ = value("Behavior/SingleClick").toBool();
    autoSelectionDelay_ = value("Behavior/AutoSelectionDelay").toInt();
    ctrlRightClick_ = value("Behavior/CtrlRightClick").toBool();
    confirmDelete_ = value("Behavior/ConfirmDelete").toBool();
    setNoUsbTrash(value("Behavior/NoUsbTrash").toBool());
    confirmTrash_ = value("Behavior/ConfirmTrash").toBool();
    setQuickExec(value("Behavior/QuickExec").toBool());
    selectNewFiles_ = value("Behavior/SelectNewFiles").toBool();
    openWithDefaultFileManager_ = value("Behavior/OpenWithDefaultFileManager").toBool();
    allSticky_ = value("Behavior/AllSticky").toBool();

    showThumbnails_ = value("Thumbnail/ShowThumbnails").toBool();
    setMaxThumbnailFileSize(value("Thumbnail/MaxThumbnailFileSize").toInt());
    setMaxExternalThumbnailFileSize(value("Thumbnail/MaxExternalThumbnailFileSize").toInt());
    setThumbnailLocalFilesOnly(value("Thumbnail/ThumbnailLocalFilesOnly").toBool());

    viewMode_ = FolderSettings::viewModeFromString(value("FolderView/Mode").toString());
    showHidden_ = value("FolderView/ShowHidden").toBool();
    sortOrder_ = FolderSettings::sortOrderFromString(value("FolderView/SortOrder").toString());
    sortColumn_ = FolderSettings::sortColumnFromString(value("FolderView/SortColumn").toString());
    sortFolderFirst_ = value("FolderView/SortFolderFirst").toBool();
    sortCaseSensitive_ = value("FolderView/SortCaseSensitive").toBool();
    showFilter_ = value("FolderView/ShowFilter").toBool();
    setBackupAsHidden(value("FolderView/BackupAsHidden").toBool());
    showFullNames_ = value("FolderView/ShowFullNames").toBool();
    shadowHidden_ = value("FolderView/ShadowHidden").toBool();
    noItemTooltip_ = value("FolderView/NoItemTooltip").toBool();
    scrollPerPixel_ = value("FolderView/ScrollPerPixel").toBool();

    bigIconSize_ = toIconSize(value("FolderView/BigIconSize").toInt(), Big);
    smallIconSize_ = toIconSize(value("FolderView/SmallIconSize").toInt(), Small);
    sidePaneIconSize_ = toIconSize(value("FolderView/SidePaneIconSize").toInt(), Small);
    thumbnailIconSize_ = toIconSize(value("FolderView/ThumbnailIconSize").toInt(), Thumbnail);
    folderViewCellMargins_ =
        value("FolderView/FolderViewCellMargins").toSize().expandedTo(QSize(0, 0)).boundedTo(QSize(48, 48));
    customColumnWidths_ = value("FolderView/CustomColumnWidths").toList();
    hiddenColumns_ = value("FolderView/HiddenColumns").toList();

    const QStringList hiddenPlacesList = value("Places/HiddenPlaces").toStringList();
    hiddenPlaces_ = QSet<QString>(hiddenPlacesList.begin(), hiddenPlacesList.end());
    hiddenPlaces_ << QStringLiteral("computer:///") << QStringLiteral("network:///") << QStringLiteral("trash:///");

    fixedWindowWidth_ = value("Window/FixedWidth").toInt();
    fixedWindowHeight_ = value("Window/FixedHeight").toInt();
    lastWindowWidth_ = value("Window/LastWindowWidth").toInt();
    lastWindowHeight_ = value("Window/LastWindowHeight").toInt();
    lastWindowX_ = value("Window/LastWindowX").toInt();
    lastWindowY_ = value("Window/LastWindowY").toInt();
    lastWindowMaximized_ = value("Window/LastWindowMaximized").toBool();
    rememberWindowSize_ = value("Window/RememberWindowSize").toBool();
    alwaysShowTabs_ = value("Window/AlwaysShowTabs").toBool();
    showTabClose_ = value("Window/ShowTabClose").toBool();
    switchToNewTab_ = value("Window/SwitchToNewTab").toBool();
    reopenLastTabs_ = value("Window/ReopenLastTabs").toBool();
    tabPaths_ = value("Window/TabPaths").toStringList();
    splitViewTabsNum_ = value("Window/SplitViewTabsNum").toInt();
    splitterPos_ = value("Window/SplitterPos").toInt();
    sidePaneVisible_ = value("Window/SidePaneVisible").toBool();
    sidePaneMode_ = FolderSettings::sidePaneModeFromString(value("Window/SidePaneMode").toString());
    showMenuBar_ = value("Window/ShowMenuBar").toBool();
    splitView_ = value("Window/SplitView").toBool();
    pathBarButtons_ = value("Window/PathBarButtons").toBool();

    searchNameCaseInsensitive_ = value("Search/searchNameCaseInsensitive").toBool();
    searchContentCaseInsensitive_ = value("Search/searchContentCaseInsensitive").toBool();
    searchNameRegexp_ = value("Search/searchNameRegexp").toBool();
    searchContentRegexp_ = value("Search/searchContentRegexp").toBool();
    searchRecursive_ = value("Search/searchRecursive").toBool();
    searchhHidden_ = value("Search/searchhHidden").toBool();
    setMaxSearchHistory(value("Search/MaxSearchHistory").toInt());
    namePatterns_ = value("Search/NamePatterns").toStringList();
    namePatterns_.removeDuplicates();
    contentPatterns_ = value("Search/ContentPatterns").toStringList();
    contentPatterns_.removeDuplicates();
    if (maxSearchHistory_ == 0) {
        clearSearchHistory();
    }
    else {
        while (namePatterns_.size() > maxSearchHistory_) {
            namePatterns_.removeLast();
        }
        while (contentPatterns_.size() > maxSearchHistory_) {
            contentPatterns_.removeLast();
        }
    }

    return true;
}

bool Settings::saveFile(QString filePath) {
    QVector<ProfileSchemaKey> schemaKeys;
    if (!loadProfileSchema(schemaKeys)) {
        return false;
    }

    QHash<QString, QVariant> values;
    values.insert(QStringLiteral("System/FallbackIconThemeName"), fallbackIconThemeName_);
    values.insert(QStringLiteral("System/Terminal"), terminal_);
    values.insert(QStringLiteral("System/Archiver"), archiver_);
    values.insert(QStringLiteral("System/SIUnit"), siUnit_);
    values.insert(QStringLiteral("System/OnlyUserTemplates"), onlyUserTemplates_);
    values.insert(QStringLiteral("System/TemplateTypeOnce"), templateTypeOnce_);
    values.insert(QStringLiteral("System/TemplateRunApp"), templateRunApp_);

    values.insert(QStringLiteral("Behavior/SingleWindowMode"), singleWindowMode_);
    values.insert(QStringLiteral("Behavior/BookmarkOpenMethod"),
                  QString::fromUtf8(bookmarkOpenMethodToString(bookmarkOpenMethod_)));
    values.insert(QStringLiteral("Behavior/PreservePermissions"), preservePermissions_);
    values.insert(QStringLiteral("Behavior/UseTrash"), useTrash_);
    values.insert(QStringLiteral("Behavior/SingleClick"), singleClick_);
    values.insert(QStringLiteral("Behavior/AutoSelectionDelay"), autoSelectionDelay_);
    values.insert(QStringLiteral("Behavior/CtrlRightClick"), ctrlRightClick_);
    values.insert(QStringLiteral("Behavior/ConfirmDelete"), confirmDelete_);
    values.insert(QStringLiteral("Behavior/NoUsbTrash"), noUsbTrash_);
    values.insert(QStringLiteral("Behavior/ConfirmTrash"), confirmTrash_);
    values.insert(QStringLiteral("Behavior/QuickExec"), quickExec_);
    values.insert(QStringLiteral("Behavior/SelectNewFiles"), selectNewFiles_);
    values.insert(QStringLiteral("Behavior/OpenWithDefaultFileManager"), openWithDefaultFileManager_);
    values.insert(QStringLiteral("Behavior/AllSticky"), allSticky_);

    values.insert(QStringLiteral("Thumbnail/ShowThumbnails"), showThumbnails_);
    values.insert(QStringLiteral("Thumbnail/MaxThumbnailFileSize"), maxThumbnailFileSize());
    values.insert(QStringLiteral("Thumbnail/MaxExternalThumbnailFileSize"), maxExternalThumbnailFileSize());
    values.insert(QStringLiteral("Thumbnail/ThumbnailLocalFilesOnly"), thumbnailLocalFilesOnly());

    values.insert(QStringLiteral("FolderView/Mode"), QString::fromUtf8(Settings::viewModeToString(viewMode_)));
    values.insert(QStringLiteral("FolderView/ShowHidden"), showHidden_);
    values.insert(QStringLiteral("FolderView/SortOrder"), QString::fromUtf8(Settings::sortOrderToString(sortOrder_)));
    values.insert(QStringLiteral("FolderView/SortColumn"),
                  QString::fromUtf8(Settings::sortColumnToString(sortColumn_)));
    values.insert(QStringLiteral("FolderView/SortFolderFirst"), sortFolderFirst_);
    values.insert(QStringLiteral("FolderView/SortCaseSensitive"), sortCaseSensitive_);
    values.insert(QStringLiteral("FolderView/ShowFilter"), showFilter_);
    values.insert(QStringLiteral("FolderView/BackupAsHidden"), backupAsHidden_);
    values.insert(QStringLiteral("FolderView/ShowFullNames"), showFullNames_);
    values.insert(QStringLiteral("FolderView/ShadowHidden"), shadowHidden_);
    values.insert(QStringLiteral("FolderView/NoItemTooltip"), noItemTooltip_);
    values.insert(QStringLiteral("FolderView/ScrollPerPixel"), scrollPerPixel_);
    values.insert(QStringLiteral("FolderView/BigIconSize"), bigIconSize_);
    values.insert(QStringLiteral("FolderView/SmallIconSize"), smallIconSize_);
    values.insert(QStringLiteral("FolderView/SidePaneIconSize"), sidePaneIconSize_);
    values.insert(QStringLiteral("FolderView/ThumbnailIconSize"), thumbnailIconSize_);
    values.insert(QStringLiteral("FolderView/FolderViewCellMargins"), folderViewCellMargins_);
    values.insert(QStringLiteral("FolderView/CustomColumnWidths"), customColumnWidths_);
    QList<int> columns = getHiddenColumns();
    std::sort(columns.begin(), columns.end());
    QVariantList hiddenColumns;
    hiddenColumns.reserve(columns.size());
    for (int column : columns) {
        hiddenColumns.append(column);
    }
    values.insert(QStringLiteral("FolderView/HiddenColumns"), hiddenColumns);

    QStringList hiddenPlacesList(hiddenPlaces_.begin(), hiddenPlaces_.end());
    hiddenPlacesList.removeAll(QString());
    hiddenPlacesList.removeDuplicates();
    std::sort(hiddenPlacesList.begin(), hiddenPlacesList.end());
    values.insert(QStringLiteral("Places/HiddenPlaces"), hiddenPlacesList);

    values.insert(QStringLiteral("Window/FixedWidth"), fixedWindowWidth_);
    values.insert(QStringLiteral("Window/FixedHeight"), fixedWindowHeight_);
    values.insert(QStringLiteral("Window/LastWindowWidth"), lastWindowWidth_);
    values.insert(QStringLiteral("Window/LastWindowHeight"), lastWindowHeight_);
    values.insert(QStringLiteral("Window/LastWindowX"), lastWindowX_);
    values.insert(QStringLiteral("Window/LastWindowY"), lastWindowY_);
    values.insert(QStringLiteral("Window/LastWindowMaximized"), lastWindowMaximized_);
    values.insert(QStringLiteral("Window/RememberWindowSize"), rememberWindowSize_);
    values.insert(QStringLiteral("Window/AlwaysShowTabs"), alwaysShowTabs_);
    values.insert(QStringLiteral("Window/ShowTabClose"), showTabClose_);
    values.insert(QStringLiteral("Window/SwitchToNewTab"), switchToNewTab_);
    values.insert(QStringLiteral("Window/ReopenLastTabs"), reopenLastTabs_);
    values.insert(QStringLiteral("Window/TabPaths"), tabPaths_);
    values.insert(QStringLiteral("Window/SplitViewTabsNum"), splitViewTabsNum_);
    values.insert(QStringLiteral("Window/SplitterPos"), splitterPos_);
    values.insert(QStringLiteral("Window/SidePaneVisible"), sidePaneVisible_);
    values.insert(QStringLiteral("Window/SidePaneMode"), QString::fromUtf8(sidePaneModeToString(sidePaneMode_)));
    values.insert(QStringLiteral("Window/ShowMenuBar"), showMenuBar_);
    values.insert(QStringLiteral("Window/SplitView"), splitView_);
    values.insert(QStringLiteral("Window/PathBarButtons"), pathBarButtons_);

    values.insert(QStringLiteral("Search/searchNameCaseInsensitive"), searchNameCaseInsensitive_);
    values.insert(QStringLiteral("Search/searchContentCaseInsensitive"), searchContentCaseInsensitive_);
    values.insert(QStringLiteral("Search/searchNameRegexp"), searchNameRegexp_);
    values.insert(QStringLiteral("Search/searchContentRegexp"), searchContentRegexp_);
    values.insert(QStringLiteral("Search/searchRecursive"), searchRecursive_);
    values.insert(QStringLiteral("Search/searchhHidden"), searchhHidden_);
    values.insert(QStringLiteral("Search/MaxSearchHistory"), maxSearchHistory_);
    values.insert(QStringLiteral("Search/NamePatterns"), namePatterns_);
    values.insert(QStringLiteral("Search/ContentPatterns"), contentPatterns_);

    const QHash<QString, QVariant> normalized = normalizeAstBySchema(schemaKeys, values);
    QSettings settings(filePath, QSettings::IniFormat);
    for (const ProfileSchemaKey& schemaKey : schemaKeys) {
        settings.setValue(schemaKey.key, normalized.value(schemaKey.key, schemaKey.defaultValue));
    }
    settings.sync();

    // save per-folder settings
    Panel::FolderConfig::saveCache();

    return settings.status() == QSettings::NoError;
}

void Settings::clearSearchHistory() {
    namePatterns_.clear();
    contentPatterns_.clear();
}

void Settings::setMaxSearchHistory(int max) {
    maxSearchHistory_ = std::max(max, 0);
    if (maxSearchHistory_ == 0) {
        namePatterns_.clear();
        contentPatterns_.clear();
    }
    else {
        while (namePatterns_.size() > maxSearchHistory_) {
            namePatterns_.removeLast();
        }
        while (contentPatterns_.size() > maxSearchHistory_) {
            contentPatterns_.removeLast();
        }
    }
}

void Settings::addNamePattern(const QString& pattern) {
    if (maxSearchHistory_ == 0 ||
        pattern.isEmpty()
        // "*" is too trivial with a regex search
        || (searchNameRegexp_ && pattern == QLatin1String("*"))) {
        return;
    }
    namePatterns_.removeOne(pattern);
    namePatterns_.prepend(pattern);
    while (namePatterns_.size() > maxSearchHistory_) {
        namePatterns_.removeLast();
    }
}

void Settings::addContentPattern(const QString& pattern) {
    if (maxSearchHistory_ == 0 || pattern.isEmpty() || (searchContentRegexp_ && pattern == QLatin1String("*"))) {
        return;
    }
    contentPatterns_.removeOne(pattern);
    contentPatterns_.prepend(pattern);
    while (contentPatterns_.size() > maxSearchHistory_) {
        contentPatterns_.removeLast();
    }
}

const QList<int>& Settings::iconSizes(IconType type) {
    static const QList<int> sizes_big = {96, 72, 64, 48, 32};
    static const QList<int> sizes_thumbnail = {256, 224, 192, 160, 128, 96, 64};
    static const QList<int> sizes_small = {48, 32, 24, 22, 16};
    switch (type) {
        case Big:
            return sizes_big;
            break;
        case Thumbnail:
            return sizes_thumbnail;
            break;
        case Small:
        default:
            return sizes_small;
            break;
    }
}

// String conversion member functions
const char* Settings::viewModeToString(Panel::FolderView::ViewMode value) {
    const char* ret;
    switch (value) {
        case Panel::FolderView::IconMode:
        default:
            ret = "icon";
            break;
        case Panel::FolderView::CompactMode:
            ret = "compact";
            break;
        case Panel::FolderView::DetailedListMode:
            ret = "detailed";
            break;
        case Panel::FolderView::ThumbnailMode:
            ret = "thumbnail";
            break;
    }
    return ret;
}

const char* Settings::sortOrderToString(Qt::SortOrder order) {
    return (order == Qt::DescendingOrder ? "descending" : "ascending");
}

const char* Settings::sortColumnToString(Panel::FolderModel::ColumnId value) {
    const char* ret;
    switch (value) {
        case Panel::FolderModel::ColumnFileName:
        default:
            ret = "name";
            break;
        case Panel::FolderModel::ColumnFileType:
            ret = "type";
            break;
        case Panel::FolderModel::ColumnFileSize:
            ret = "size";
            break;
        case Panel::FolderModel::ColumnFileMTime:
            ret = "mtime";
            break;
        case Panel::FolderModel::ColumnFileCrTime:
            ret = "crtime";
            break;
        case Panel::FolderModel::ColumnFileDTime:
            ret = "dtime";
            break;
        case Panel::FolderModel::ColumnFileOwner:
            ret = "owner";
            break;
        case Panel::FolderModel::ColumnFileGroup:
            ret = "group";
            break;
    }
    return ret;
}

const char* Settings::sidePaneModeToString(Panel::SidePane::Mode value) {
    const char* ret;
    switch (value) {
        case Panel::SidePane::ModePlaces:
        default:
            ret = "places";
            break;
        case Panel::SidePane::ModeDirTree:
            ret = "dirtree";
            break;
        case Panel::SidePane::ModeNone:
            ret = "none";
            break;
    }
    return ret;
}

int Settings::toIconSize(int size, IconType type) const {
    const QList<int>& sizes = iconSizes(type);
    for (const auto& s : sizes) {
        if (size >= s) {
            return s;
        }
    }
    return sizes.back();
}

static const char* bookmarkOpenMethodToString(OpenDirTargetType value) {
    switch (value) {
        case OpenInCurrentTab:
        default:
            return "current_tab";
        case OpenInNewTab:
            return "new_tab";
        case OpenInNewWindow:
            return "new_window";
        case OpenInLastActiveWindow:
            return "last_window";
    }
    return "";
}

OpenDirTargetType FolderSettings::bookmarkOpenMethodFromString(const QString& str) {
    if (str == QStringLiteral("new_tab")) {
        return OpenInNewTab;
    }
    else if (str == QStringLiteral("new_window")) {
        return OpenInNewWindow;
    }
    else if (str == QStringLiteral("last_window")) {
        return OpenInLastActiveWindow;
    }
    return OpenInCurrentTab;
}

Panel::FolderView::ViewMode FolderSettings::viewModeFromString(const QString& str) {
    Panel::FolderView::ViewMode ret;
    if (str == QLatin1String("icon")) {
        ret = Panel::FolderView::IconMode;
    }
    else if (str == QLatin1String("compact")) {
        ret = Panel::FolderView::CompactMode;
    }
    else if (str == QLatin1String("detailed")) {
        ret = Panel::FolderView::DetailedListMode;
    }
    else if (str == QLatin1String("thumbnail")) {
        ret = Panel::FolderView::ThumbnailMode;
    }
    else {
        ret = Panel::FolderView::DetailedListMode;
    }
    return ret;
}

Qt::SortOrder FolderSettings::sortOrderFromString(const QString& str) {
    return (str == QLatin1String("descending") ? Qt::DescendingOrder : Qt::AscendingOrder);
}

Panel::FolderModel::ColumnId FolderSettings::sortColumnFromString(const QString& str) {
    Panel::FolderModel::ColumnId ret;
    if (str == QLatin1String("name")) {
        ret = Panel::FolderModel::ColumnFileName;
    }
    else if (str == QLatin1String("type")) {
        ret = Panel::FolderModel::ColumnFileType;
    }
    else if (str == QLatin1String("size")) {
        ret = Panel::FolderModel::ColumnFileSize;
    }
    else if (str == QLatin1String("mtime")) {
        ret = Panel::FolderModel::ColumnFileMTime;
    }
    else if (str == QLatin1String("crtime")) {
        ret = Panel::FolderModel::ColumnFileCrTime;
    }
    else if (str == QLatin1String("dtime")) {
        ret = Panel::FolderModel::ColumnFileDTime;
    }
    else if (str == QLatin1String("owner")) {
        ret = Panel::FolderModel::ColumnFileOwner;
    }
    else if (str == QLatin1String("group")) {
        ret = Panel::FolderModel::ColumnFileGroup;
    }
    else {
        ret = Panel::FolderModel::ColumnFileName;
    }
    return ret;
}

Panel::SidePane::Mode FolderSettings::sidePaneModeFromString(const QString& str) {
    Panel::SidePane::Mode ret;
    if (str == QLatin1String("none")) {
        ret = Panel::SidePane::ModeNone;
    }
    else if (str == QLatin1String("dirtree")) {
        ret = Panel::SidePane::ModeDirTree;
    }
    else {
        ret = Panel::SidePane::ModePlaces;
    }
    return ret;
}

void Settings::setTerminal(QString terminalCommand) {
    terminal_ = terminalCommand;
    Panel::setDefaultTerminal(terminal_.toStdString());
}

// per-folder settings
FolderSettings Settings::loadFolderSettings(const Panel::FilePath& path) const {
    FolderSettings settings;
    Panel::FolderConfig cfg(path);
    bool customized = !cfg.isEmpty();
    Panel::FilePath inheritedPath;
    if (!customized && !path.isParentOf(path)) {  // WARNING: menu://applications/ is its own parent
        inheritedPath = path.parent();
        while (inheritedPath.isValid()) {
            Panel::GErrorPtr err;
            cfg.close(err);
            cfg.open(inheritedPath);
            if (!cfg.isEmpty()) {
                bool recursive;
                if (cfg.getBoolean("Recursive", &recursive) && recursive) {
                    break;
                }
            }
            if (inheritedPath.isParentOf(inheritedPath)) {
                inheritedPath = Panel::FilePath();  // invalidate it
                break;
            }
            inheritedPath = inheritedPath.parent();
        }
    }
    if (!customized && !inheritedPath.isValid()) {
        // the folder is not customized and does not inherit settings; use the general settings
        settings.setSortOrder(sortOrder());
        settings.setSortColumn(sortColumn());
        settings.setViewMode(viewMode());
        settings.setShowHidden(showHidden());
        settings.setSortFolderFirst(sortFolderFirst());
        settings.setSortCaseSensitive(sortCaseSensitive());
    }
    else {
        // either the folder is customized or it inherits settings; load folder-specific settings
        if (!inheritedPath.isValid()) {
            settings.setCustomized(true);
        }
        else {
            settings.seInheritedPath(inheritedPath);
        }

        char* str;
        // load sorting
        str = cfg.getString("SortOrder");
        if (str != nullptr) {
            settings.setSortOrder(FolderSettings::sortOrderFromString(QString::fromUtf8(str)));
            g_free(str);
        }

        str = cfg.getString("SortColumn");
        if (str != nullptr) {
            settings.setSortColumn(FolderSettings::sortColumnFromString(QString::fromUtf8(str)));
            g_free(str);
        }

        str = cfg.getString("ViewMode");
        if (str != nullptr) {
            // set view mode
            settings.setViewMode(FolderSettings::viewModeFromString(QString::fromUtf8(str)));
            g_free(str);
        }

        bool show_hidden;
        if (cfg.getBoolean("ShowHidden", &show_hidden)) {
            settings.setShowHidden(show_hidden);
        }

        bool folder_first;
        if (cfg.getBoolean("SortFolderFirst", &folder_first)) {
            settings.setSortFolderFirst(folder_first);
        }

        bool case_sensitive;
        if (cfg.getBoolean("SortCaseSensitive", &case_sensitive)) {
            settings.setSortCaseSensitive(case_sensitive);
        }

        bool recursive;
        if (cfg.getBoolean("Recursive", &recursive)) {
            settings.setRecursive(recursive);
        }
    }
    return settings;
}

void Settings::saveFolderSettings(const Panel::FilePath& path, const FolderSettings& settings) {
    if (path) {
        // ensure that we have the libfm dir
        QString dirName = xdgUserConfigDir() + QStringLiteral("/libfm");
        QString error;
        FsQt::makeDirParents(dirName, error);  // if libfm config dir does not exist, create it

        Panel::FolderConfig cfg(path);
        cfg.setString("SortOrder", sortOrderToString(settings.sortOrder()));
        cfg.setString("SortColumn", sortColumnToString(settings.sortColumn()));
        cfg.setString("ViewMode", viewModeToString(settings.viewMode()));
        cfg.setBoolean("ShowHidden", settings.showHidden());
        cfg.setBoolean("SortFolderFirst", settings.sortFolderFirst());
        cfg.setBoolean("SortCaseSensitive", settings.sortCaseSensitive());
        cfg.setBoolean("Recursive", settings.recursive());
    }
}

void Settings::clearFolderSettings(const Panel::FilePath& path) const {
    if (path) {
        Panel::FolderConfig cfg(path);
        cfg.purge();
    }
}

}  // namespace Oneg4FM
