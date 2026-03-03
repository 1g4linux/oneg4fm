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
#include <cerrno>
#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include "../src/ui/fsqt.h"

namespace {

constexpr const char* kSchemaPathEnv = "ONEG4FM_SCHEMA_PATH";
constexpr const char* kInstalledSchemaRelativePath = "oneg4fm/default/schema.json";
constexpr const char* kSourceSchemaRelativePath = "config/oneg4fm/schema.json";
constexpr const char* kProfileSchemaSurfaceId = "profile.settings.conf";
constexpr const char* kDirectorySchemaSurfaceId = "directory.dir-settings.conf";
constexpr const char* kStrictUnknownKeysEnv = "ONEG4FM_SETTINGS_STRICT_UNKNOWN_KEYS";
constexpr int CURRENT_SCHEMA_VERSION = 1;
constexpr int SUPPORTED_SCHEMA_BACKWARD_WINDOW = 0;
constexpr const char* kProfileSchemaVersionKey = "Meta/schema_version";
constexpr const char* kDirectorySchemaVersionKey = "schema_version";
constexpr const char* kLegacyProfileSearchHiddenKey = "Search/searchHidden";
constexpr const char* kProfileSearchHiddenKey = "Search/searchhHidden";
constexpr const char* kSettingsTmpSuffix = ".tmp";
constexpr const char* kSettingsDefaultsSourceToken = "schema_defaults";
constexpr qint64 kMaxProfileSettingsFileBytes = 1024 * 1024;
constexpr int kMaxProfileSettingsLineCount = 16384;
constexpr int kMaxProfileSettingsLineBytes = 16384;

enum class UnknownKeyPolicy { Preserve };

struct SchemaKey {
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

bool loadSchemaSurface(const QString& surfaceId, QVector<SchemaKey>& schemaKeys, QString* schemaPathOut = nullptr) {
    schemaKeys.clear();
    if (schemaPathOut != nullptr) {
        schemaPathOut->clear();
    }

    const QString schemaPath = resolveSchemaPath();
    if (schemaPath.isEmpty()) {
        return false;
    }
    if (schemaPathOut != nullptr) {
        *schemaPathOut = schemaPath;
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
        if (surface.value(QStringLiteral("id")).toString() != surfaceId) {
            continue;
        }

        const QJsonArray keys = surface.value(QStringLiteral("keys")).toArray();
        schemaKeys.reserve(keys.size());
        for (const QJsonValue& keyValue : keys) {
            if (!keyValue.isObject()) {
                continue;
            }

            const QJsonObject keyObj = keyValue.toObject();
            SchemaKey schemaKey;
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

bool loadProfileSchema(QVector<SchemaKey>& schemaKeys, QString* schemaPathOut = nullptr) {
    return loadSchemaSurface(QString::fromUtf8(kProfileSchemaSurfaceId), schemaKeys, schemaPathOut);
}

bool loadDirectorySchema(QVector<SchemaKey>& schemaKeys, QString* schemaPathOut = nullptr) {
    return loadSchemaSurface(QString::fromUtf8(kDirectorySchemaSurfaceId), schemaKeys, schemaPathOut);
}

QSet<QString> schemaKeySet(const QVector<SchemaKey>& schemaKeys) {
    QSet<QString> knownKeys;
    knownKeys.reserve(schemaKeys.size());
    for (const SchemaKey& schemaKey : schemaKeys) {
        knownKeys.insert(schemaKey.key);
    }
    return knownKeys;
}

QHash<QString, QVariant> collectUnknownAstEntries(const QHash<QString, QVariant>& ast, const QSet<QString>& knownKeys) {
    QHash<QString, QVariant> unknownEntries;
    for (auto it = ast.cbegin(); it != ast.cend(); ++it) {
        if (!knownKeys.contains(it.key())) {
            unknownEntries.insert(it.key(), it.value());
        }
    }
    return unknownEntries;
}

QStringList sortedUnknownKeys(const QHash<QString, QVariant>& unknownEntries) {
    QStringList keys;
    keys.reserve(unknownEntries.size());
    for (auto it = unknownEntries.cbegin(); it != unknownEntries.cend(); ++it) {
        keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

QHash<QString, int> readIniKeyLineNumbers(const QString& filePath) {
    QHash<QString, int> lineNumbers;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return lineNumbers;
    }

    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QLatin1Char('\n'));
    QString currentSection;
    int lineNumber = 0;
    for (const QString& rawLine : lines) {
        ++lineNumber;
        const QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';')) || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (trimmed.startsWith(QLatin1Char('['))) {
            const int closeBracket = trimmed.indexOf(QLatin1Char(']'));
            if (closeBracket > 1) {
                currentSection = trimmed.mid(1, closeBracket - 1).trimmed();
            }
            continue;
        }

        const int separator = trimmed.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        const QString key = trimmed.left(separator).trimmed();
        if (key.isEmpty()) {
            continue;
        }

        const QString fullKey = currentSection.isEmpty() ? key : QStringLiteral("%1/%2").arg(currentSection, key);
        lineNumbers.insert(fullKey, lineNumber);
    }

    return lineNumbers;
}

QStringList formatUnknownKeysForDiagnostics(const QHash<QString, QVariant>& unknownEntries, const QString& filePath) {
    const QStringList keys = sortedUnknownKeys(unknownEntries);
    if (keys.isEmpty()) {
        return {};
    }

    const QHash<QString, int> lineNumbers = readIniKeyLineNumbers(filePath);
    QStringList formatted;
    formatted.reserve(keys.size());
    for (const QString& key : keys) {
        const auto it = lineNumbers.constFind(key);
        if (it == lineNumbers.cend()) {
            formatted.push_back(key);
            continue;
        }
        formatted.push_back(QStringLiteral("%1 (line %2)").arg(key).arg(*it));
    }
    return formatted;
}

bool strictUnknownKeysEnabled() {
    const QByteArray raw = qgetenv(kStrictUnknownKeysEnv);
    if (raw.isEmpty()) {
        return false;
    }

    const QByteArray token = raw.trimmed().toLower();
    return token == QByteArrayLiteral("1") || token == QByteArrayLiteral("true") || token == QByteArrayLiteral("yes") ||
           token == QByteArrayLiteral("on");
}

UnknownKeyPolicy profileUnknownKeyPolicy() {
    return UnknownKeyPolicy::Preserve;
}

enum class IniReadStatus { Ok, Missing, Invalid };

struct IniReadMetadata {
    QStringList sourcesUsed;
    QStringList errors;
};

QString settingsTempPath(const QString& filePath) {
    return filePath + QString::fromUtf8(kSettingsTmpSuffix);
}

bool settingsFileWithinBounds(const QString& filePath) {
    const QFileInfo info(filePath);
    if (!info.exists()) {
        return true;
    }
    if (!info.isFile()) {
        return false;
    }
    if (info.size() > kMaxProfileSettingsFileBytes) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray content = file.readAll();
    if (content.size() > kMaxProfileSettingsFileBytes) {
        return false;
    }

    int lines = content.isEmpty() ? 0 : 1;
    int lineLength = 0;
    for (const char ch : content) {
        if (lineLength > kMaxProfileSettingsLineBytes) {
            return false;
        }
        if (ch == '\n') {
            ++lines;
            lineLength = 0;
            if (lines > kMaxProfileSettingsLineCount) {
                return false;
            }
            continue;
        }
        ++lineLength;
    }
    return lineLength <= kMaxProfileSettingsLineBytes;
}

IniReadStatus readIniAstFile(const QString& filePath, QHash<QString, QVariant>& ast, QString* errorOut = nullptr) {
    ast.clear();
    if (errorOut != nullptr) {
        errorOut->clear();
    }
    const QFileInfo info(filePath);
    if (!info.exists()) {
        return IniReadStatus::Missing;
    }
    if (!settingsFileWithinBounds(filePath)) {
        const QString message = QStringLiteral("Settings file rejected by bounds: %1").arg(filePath);
        qWarning().noquote() << message;
        if (errorOut != nullptr) {
            *errorOut = message;
        }
        return IniReadStatus::Invalid;
    }

    QSettings settings(filePath, QSettings::IniFormat);
    for (const QString& key : settings.allKeys()) {
        ast.insert(key, settings.value(key));
    }
    if (settings.status() != QSettings::NoError) {
        const QString message = QStringLiteral("Settings file parse error: %1").arg(filePath);
        qWarning().noquote() << message;
        if (errorOut != nullptr) {
            *errorOut = message;
        }
        ast.clear();
        return IniReadStatus::Invalid;
    }
    return IniReadStatus::Ok;
}

bool syncDirectoryAfterRename(const QString& filePath) {
    const QByteArray dirUtf8 = QFileInfo(filePath).absoluteDir().absolutePath().toUtf8();
    const int dirFd = ::open(dirUtf8.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd < 0) {
        return false;
    }
    bool ok = true;
    while (::fsync(dirFd) < 0) {
        if (errno == EINTR) {
            continue;
        }
        ok = false;
        break;
    }
    ::close(dirFd);
    return ok;
}

bool writeAllBytes(int fd, const QByteArray& payload) {
    const char* cursor = payload.constData();
    qint64 remaining = payload.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, static_cast<size_t>(remaining));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

QString serializeIniValue(const QVariant& value) {
    if (!value.isValid() || value.isNull()) {
        return QString();
    }

    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    if (value.metaType().id() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        QStringList tokens;
        tokens.reserve(list.size());
        for (const QVariant& entry : list) {
            tokens.append(entry.toString().trimmed());
        }
        return tokens.join(QLatin1Char(','));
    }

    if (value.metaType().id() == QMetaType::QStringList) {
        QStringList tokens = value.toStringList();
        for (QString& token : tokens) {
            token = token.trimmed();
        }
        return tokens.join(QLatin1Char(','));
    }

    if (value.canConvert<QSize>()) {
        const QSize size = value.toSize();
        if (size.width() != -1 || size.height() != -1) {
            return QStringLiteral("@Size(%1 %2)").arg(size.width()).arg(size.height());
        }
    }

    return value.toString();
}

QByteArray renderCanonicalIni(const QStringList& orderedKeys, const QHash<QString, QVariant>& values) {
    QByteArray out;
    QString currentSection;
    bool hasWrittenAnyKey = false;
    for (const QString& fullKey : orderedKeys) {
        if (!values.contains(fullKey)) {
            continue;
        }

        const int separator = fullKey.indexOf(QLatin1Char('/'));
        const QString section = separator >= 0 ? fullKey.left(separator) : QString();
        const QString key = separator >= 0 ? fullKey.mid(separator + 1) : fullKey;
        if (key.isEmpty()) {
            continue;
        }

        if (section != currentSection) {
            if (hasWrittenAnyKey) {
                out.append('\n');
            }
            if (!section.isEmpty()) {
                out.append('[');
                out.append(section.toUtf8());
                out.append("]\n");
            }
            currentSection = section;
        }

        out.append(key.toUtf8());
        out.append('=');
        out.append(serializeIniValue(values.value(fullKey)).toUtf8());
        out.append('\n');
        hasWrittenAnyKey = true;
    }
    return out;
}

bool atomicWriteIniFile(const QString& filePath, const QByteArray& payload) {
    const QFileInfo info(filePath);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    const QString tempPath = settingsTempPath(filePath);
    const QByteArray tempPathUtf8 = tempPath.toUtf8();
    const int fd = ::open(tempPathUtf8.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }

    bool ok = writeAllBytes(fd, payload);
    if (ok) {
        while (::fsync(fd) < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
    }

    if (::close(fd) < 0) {
        ok = false;
    }
    if (!ok) {
        QFile::remove(tempPath);
        return false;
    }

    QFile::remove(filePath);
    if (::rename(tempPathUtf8.constData(), filePath.toUtf8().constData()) < 0) {
        QFile::remove(tempPath);
        return false;
    }

    if (!syncDirectoryAfterRename(filePath)) {
        return false;
    }

    return true;
}

bool readIniAst(const QString& filePath, QHash<QString, QVariant>& ast, IniReadMetadata* metadata = nullptr) {
    if (metadata != nullptr) {
        metadata->sourcesUsed.clear();
        metadata->errors.clear();
    }

    const QString absolutePrimaryPath = QFileInfo(filePath).absoluteFilePath();
    const QString tempPath = settingsTempPath(filePath);
    const QString absoluteTempPath = QFileInfo(tempPath).absoluteFilePath();

    QHash<QString, QVariant> primaryAst;
    QString primaryError;
    const IniReadStatus primaryStatus = readIniAstFile(filePath, primaryAst, &primaryError);
    if (primaryStatus == IniReadStatus::Ok) {
        ast = std::move(primaryAst);
        if (metadata != nullptr) {
            metadata->sourcesUsed.append(absolutePrimaryPath);
        }
        if (QFileInfo::exists(tempPath)) {
            QFile::remove(tempPath);
        }
        return true;
    }

    QHash<QString, QVariant> tempAst;
    QString tempError;
    const IniReadStatus tempStatus = readIniAstFile(tempPath, tempAst, &tempError);
    if (tempStatus == IniReadStatus::Ok) {
        ast = std::move(tempAst);
        if (metadata != nullptr) {
            metadata->sourcesUsed.append(absoluteTempPath);
        }
        QFile::remove(filePath);
        if (!QFile::rename(tempPath, filePath)) {
            qWarning().noquote() << QStringLiteral("Failed to recover settings temp file: %1").arg(tempPath);
        }
        return true;
    }

    if (primaryStatus == IniReadStatus::Missing) {
        if (tempStatus == IniReadStatus::Missing || tempStatus == IniReadStatus::Invalid) {
            if (tempStatus == IniReadStatus::Invalid) {
                QFile::remove(tempPath);
                if (metadata != nullptr && !tempError.isEmpty()) {
                    metadata->errors.append(tempError);
                }
            }
            ast.clear();
            if (metadata != nullptr) {
                metadata->sourcesUsed.append(QString::fromUtf8(kSettingsDefaultsSourceToken));
            }
            return true;
        }
    }

    ast.clear();
    if (metadata != nullptr) {
        if (primaryStatus == IniReadStatus::Invalid && !primaryError.isEmpty()) {
            metadata->errors.append(primaryError);
        }
        if (tempStatus == IniReadStatus::Invalid && !tempError.isEmpty()) {
            metadata->errors.append(tempError);
        }
    }
    return false;
}

QHash<QString, QVariant> readFolderConfigAst(Panel::FolderConfig& cfg, const QVector<SchemaKey>& schemaKeys) {
    QHash<QString, QVariant> ast;
    ast.reserve(schemaKeys.size());
    for (const SchemaKey& schemaKey : schemaKeys) {
        const QByteArray keyUtf8 = schemaKey.key.toUtf8();
        char* rawValue = cfg.getString(keyUtf8.constData());
        if (rawValue != nullptr) {
            ast.insert(schemaKey.key, QString::fromUtf8(rawValue));
            g_free(rawValue);
        }
    }
    return ast;
}

enum class SettingsSurface { Profile, Directory };

enum class MigrationActionType { SchemaVersionBump, RenameKey, ValueTransform };

struct MigrationAction {
    MigrationActionType type;
    QString key;
    QString detail;
};

struct MigrationReport {
    SettingsSurface surface;
    int sourceVersion = 0;
    int targetVersion = 0;
    QVector<MigrationAction> actions;
};

void recordMigrationAction(MigrationReport& report,
                           MigrationActionType type,
                           const QString& key,
                           const QString& detail = QString()) {
    report.actions.push_back(MigrationAction{type, key, detail});
}

QString migrationActionTypeToToken(MigrationActionType type) {
    switch (type) {
        case MigrationActionType::SchemaVersionBump:
            return QStringLiteral("schema_version_bump");
        case MigrationActionType::RenameKey:
            return QStringLiteral("rename_key");
        case MigrationActionType::ValueTransform:
            return QStringLiteral("value_transform");
    }
    return QStringLiteral("unknown");
}

QStringList formatMigrationActions(const MigrationReport& report) {
    QStringList formatted;
    formatted.reserve(report.actions.size());
    for (const MigrationAction& action : report.actions) {
        formatted.append(
            QStringLiteral("%1|%2|%3").arg(migrationActionTypeToToken(action.type), action.key, action.detail));
    }
    return formatted;
}

QStringList deprecatedKeysUsed(const MigrationReport& report) {
    QSet<QString> keys;
    for (const MigrationAction& action : report.actions) {
        if (action.type == MigrationActionType::RenameKey && !action.key.isEmpty()) {
            keys.insert(action.key);
        }
    }
    QStringList sorted = keys.values();
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

QJsonArray toJsonArray(const QStringList& values) {
    QJsonArray out;
    for (const QString& value : values) {
        out.append(value);
    }
    return out;
}

QJsonObject diagnosticsReportToJsonObject(const Oneg4FM::SettingsDiagnosticsReport& report) {
    QJsonObject obj;
    obj.insert(QStringLiteral("profile_name"), report.profileName);
    obj.insert(QStringLiteral("profile_settings_path"), report.profileSettingsPath);
    obj.insert(QStringLiteral("schema_path"), report.schemaPath);
    obj.insert(QStringLiteral("schema_version"), report.schemaVersion);
    obj.insert(QStringLiteral("source_schema_version"), report.sourceSchemaVersion);
    obj.insert(QStringLiteral("target_schema_version"), report.targetSchemaVersion);
    obj.insert(QStringLiteral("sources_used"), toJsonArray(report.sourcesUsed));
    obj.insert(QStringLiteral("unknown_keys"), toJsonArray(report.unknownKeys));
    obj.insert(QStringLiteral("deprecated_keys_used"), toJsonArray(report.deprecatedKeysUsed));
    obj.insert(QStringLiteral("migration_actions"), toJsonArray(report.migrationActions));
    obj.insert(QStringLiteral("errors"), toJsonArray(report.errors));
    obj.insert(QStringLiteral("valid"), report.errors.isEmpty());
    return obj;
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

int oldestSupportedSchemaVersion() {
    return std::max(1, CURRENT_SCHEMA_VERSION - SUPPORTED_SCHEMA_BACKWARD_WINDOW);
}

int normalizeSchemaVersion(const QVariant& rawValue) {
    return parseIntValue(rawValue, CURRENT_SCHEMA_VERSION);
}

bool isSupportedSchemaVersion(int schemaVersion) {
    return schemaVersion >= oldestSupportedSchemaVersion() && schemaVersion <= CURRENT_SCHEMA_VERSION;
}

bool hasSupportedSchemaVersion(const QHash<QString, QVariant>& normalized, const QString& schemaVersionKey) {
    return isSupportedSchemaVersion(normalizeSchemaVersion(normalized.value(schemaVersionKey)));
}

int readSchemaVersionFromRawAst(const QHash<QString, QVariant>& ast, const QString& schemaVersionKey) {
    if (!ast.contains(schemaVersionKey)) {
        return 0;
    }
    return parseIntValue(ast.value(schemaVersionKey), 0);
}

void applyProfileMigrationV0ToV1(QHash<QString, QVariant>& ast, MigrationReport& report) {
    const QString legacySearchHiddenKey = QString::fromUtf8(kLegacyProfileSearchHiddenKey);
    const QString currentSearchHiddenKey = QString::fromUtf8(kProfileSearchHiddenKey);
    if (ast.contains(legacySearchHiddenKey) && !ast.contains(currentSearchHiddenKey)) {
        ast.insert(currentSearchHiddenKey, ast.value(legacySearchHiddenKey));
        recordMigrationAction(report, MigrationActionType::RenameKey, legacySearchHiddenKey,
                              QStringLiteral("migrated to Search/searchhHidden"));
    }
}

void applyDirectoryMigrationV0ToV1(QHash<QString, QVariant>& ast, MigrationReport& report) {
    const QString sortOrderKey = QStringLiteral("SortOrder");
    const QString token = ast.value(sortOrderKey).toString().trimmed().toLower();
    if (token == QLatin1String("desc")) {
        ast.insert(sortOrderKey, QStringLiteral("descending"));
        recordMigrationAction(report, MigrationActionType::ValueTransform, sortOrderKey,
                              QStringLiteral("desc -> descending"));
    }
    else if (token == QLatin1String("asc")) {
        ast.insert(sortOrderKey, QStringLiteral("ascending"));
        recordMigrationAction(report, MigrationActionType::ValueTransform, sortOrderKey,
                              QStringLiteral("asc -> ascending"));
    }
}

bool migrateAstForward(SettingsSurface surface,
                       const QString& schemaVersionKey,
                       QHash<QString, QVariant>& ast,
                       MigrationReport* outReport = nullptr) {
    MigrationReport report;
    report.surface = surface;
    report.sourceVersion = readSchemaVersionFromRawAst(ast, schemaVersionKey);
    if (report.sourceVersion > CURRENT_SCHEMA_VERSION) {
        return false;
    }

    int version = report.sourceVersion;
    while (version < CURRENT_SCHEMA_VERSION) {
        if (version == 0) {
            if (surface == SettingsSurface::Profile) {
                applyProfileMigrationV0ToV1(ast, report);
            }
            else {
                applyDirectoryMigrationV0ToV1(ast, report);
            }

            version = 1;
            ast.insert(schemaVersionKey, version);
            recordMigrationAction(report, MigrationActionType::SchemaVersionBump, schemaVersionKey,
                                  QStringLiteral("0 -> 1"));
            continue;
        }

        return false;
    }

    report.targetVersion = version;
    if (outReport != nullptr) {
        *outReport = report;
    }
    return isSupportedSchemaVersion(version);
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

QVariant normalizeValueBySchema(const SchemaKey& schemaKey, const QVariant& rawValue) {
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

QHash<QString, QVariant> normalizeAstBySchema(const QVector<SchemaKey>& schemaKeys,
                                              const QHash<QString, QVariant>& ast) {
    QHash<QString, QVariant> normalized;
    normalized.reserve(schemaKeys.size());
    for (const SchemaKey& schemaKey : schemaKeys) {
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
      maxSearchHistory_(0),
      lastProfileMigrationSourceVersion_(0),
      lastProfileMigrationTargetVersion_(0),
      lastDirectoryMigrationSourceVersion_(0),
      lastDirectoryMigrationTargetVersion_(0) {}

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
    loadedProfileSettingsPath_.clear();
    preservedUnknownProfileKeys_.clear();
    lastProfileMigrationSourceVersion_ = 0;
    lastProfileMigrationTargetVersion_ = 0;
    lastProfileMigrationActions_.clear();
    lastProfileDiagnosticsReport_ = SettingsDiagnosticsReport();
    lastProfileDiagnosticsReport_.profileName = profileName_;
    lastProfileDiagnosticsReport_.profileSettingsPath = QFileInfo(filePath).absoluteFilePath();
    lastProfileDiagnosticsReport_.schemaVersion = CURRENT_SCHEMA_VERSION;

    QVector<SchemaKey> schemaKeys;
    QString schemaPath;
    if (!loadProfileSchema(schemaKeys, &schemaPath)) {
        lastProfileDiagnosticsReport_.errors.append(QStringLiteral("Failed to load profile settings schema"));
        return false;
    }
    lastProfileDiagnosticsReport_.schemaPath = schemaPath;
    appendIfUnique(lastProfileDiagnosticsReport_.sourcesUsed, schemaPath);

    QHash<QString, QVariant> ast;
    IniReadMetadata readMetadata;
    if (!readIniAst(filePath, ast, &readMetadata)) {
        if (!readMetadata.errors.isEmpty()) {
            lastProfileDiagnosticsReport_.errors.append(readMetadata.errors);
        }
        else {
            lastProfileDiagnosticsReport_.errors.append(
                QStringLiteral("Failed to load settings file: %1").arg(filePath));
        }
        return false;
    }
    for (const QString& source : readMetadata.sourcesUsed) {
        appendIfUnique(lastProfileDiagnosticsReport_.sourcesUsed, source);
    }

    const int rawSchemaVersion = readSchemaVersionFromRawAst(ast, QString::fromUtf8(kProfileSchemaVersionKey));
    MigrationReport migrationReport;
    if (!migrateAstForward(SettingsSurface::Profile, QString::fromUtf8(kProfileSchemaVersionKey), ast,
                           &migrationReport)) {
        lastProfileDiagnosticsReport_.sourceSchemaVersion = rawSchemaVersion;
        lastProfileDiagnosticsReport_.targetSchemaVersion = rawSchemaVersion;
        lastProfileDiagnosticsReport_.errors.append(
            QStringLiteral("Unsupported profile schema version in %1: %2 (supported: %3-%4)")
                .arg(filePath)
                .arg(rawSchemaVersion)
                .arg(oldestSupportedSchemaVersion())
                .arg(CURRENT_SCHEMA_VERSION));
        return false;
    }
    lastProfileMigrationSourceVersion_ = migrationReport.sourceVersion;
    lastProfileMigrationTargetVersion_ = migrationReport.targetVersion;
    lastProfileMigrationActions_ = formatMigrationActions(migrationReport);
    lastProfileDiagnosticsReport_.sourceSchemaVersion = migrationReport.sourceVersion;
    lastProfileDiagnosticsReport_.targetSchemaVersion = migrationReport.targetVersion;
    lastProfileDiagnosticsReport_.migrationActions = lastProfileMigrationActions_;
    lastProfileDiagnosticsReport_.deprecatedKeysUsed = deprecatedKeysUsed(migrationReport);

    const QHash<QString, QVariant> unknownKeys = collectUnknownAstEntries(ast, schemaKeySet(schemaKeys));
    lastProfileDiagnosticsReport_.unknownKeys = formatUnknownKeysForDiagnostics(unknownKeys, filePath);
    if (!unknownKeys.isEmpty()) {
        const QString unknownKeyMessage =
            QStringLiteral("Unknown settings keys in %1: %2")
                .arg(filePath, lastProfileDiagnosticsReport_.unknownKeys.join(QStringLiteral(", ")));
        qWarning().noquote() << unknownKeyMessage;
        if (strictUnknownKeysEnabled()) {
            lastProfileDiagnosticsReport_.errors.append(unknownKeyMessage);
            return false;
        }
    }

    const QHash<QString, QVariant> normalized = normalizeAstBySchema(schemaKeys, ast);
    const auto value = [&](const char* key) { return normalized.value(QString::fromUtf8(key)); };
    if (!isSupportedSchemaVersion(normalizeSchemaVersion(value(kProfileSchemaVersionKey)))) {
        lastProfileDiagnosticsReport_.errors.append(
            QStringLiteral("Normalized settings have unsupported schema version in %1").arg(filePath));
        return false;
    }

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

    if (profileUnknownKeyPolicy() == UnknownKeyPolicy::Preserve) {
        loadedProfileSettingsPath_ = QFileInfo(filePath).absoluteFilePath();
        preservedUnknownProfileKeys_ = unknownKeys;
    }

    return true;
}

QHash<QString, QVariant> Settings::buildProfileAstForPersistence() const {
    QHash<QString, QVariant> values;
    values.insert(QString::fromUtf8(kProfileSchemaVersionKey), CURRENT_SCHEMA_VERSION);
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

    return values;
}

bool Settings::saveFile(QString filePath) {
    QVector<SchemaKey> schemaKeys;
    if (!loadProfileSchema(schemaKeys)) {
        return false;
    }

    QHash<QString, QVariant> values = buildProfileAstForPersistence();

    QHash<QString, QVariant> persistedValues = normalizeAstBySchema(schemaKeys, values);
    QStringList orderedKeys;
    orderedKeys.reserve(schemaKeys.size() + preservedUnknownProfileKeys_.size());
    for (const SchemaKey& schemaKey : schemaKeys) {
        orderedKeys.append(schemaKey.key);
    }

    if (profileUnknownKeyPolicy() == UnknownKeyPolicy::Preserve &&
        QFileInfo(filePath).absoluteFilePath() == loadedProfileSettingsPath_) {
        const QStringList unknownKeys = sortedUnknownKeys(preservedUnknownProfileKeys_);
        for (const QString& key : unknownKeys) {
            if (!persistedValues.contains(key)) {
                orderedKeys.append(key);
            }
            persistedValues.insert(key, preservedUnknownProfileKeys_.value(key));
        }
    }

    if (!atomicWriteIniFile(filePath, renderCanonicalIni(orderedKeys, persistedValues))) {
        return false;
    }

    // save per-folder settings
    Panel::FolderConfig::saveCache();

    return true;
}

QString Settings::lastProfileDiagnosticsReportJson() const {
    const QJsonDocument reportDoc(diagnosticsReportToJsonObject(lastProfileDiagnosticsReport_));
    return QString::fromUtf8(reportDoc.toJson(QJsonDocument::Indented));
}

bool Settings::dumpNormalizedEffectiveProfileSettings(QString* output, QString* errorMessage) {
    if (output == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Output target is null");
        }
        return false;
    }
    output->clear();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QVector<SchemaKey> schemaKeys;
    if (!loadProfileSchema(schemaKeys)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to load profile settings schema");
        }
        return false;
    }

    const QHash<QString, QVariant> normalized = normalizeAstBySchema(schemaKeys, buildProfileAstForPersistence());
    QStringList orderedKeys;
    orderedKeys.reserve(schemaKeys.size());
    for (const SchemaKey& schemaKey : schemaKeys) {
        orderedKeys.append(schemaKey.key);
    }
    *output = renderCanonicalIni(orderedKeys, normalized);
    return true;
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
    lastDirectoryMigrationSourceVersion_ = 0;
    lastDirectoryMigrationTargetVersion_ = 0;
    lastDirectoryMigrationActions_.clear();

    const auto applyGlobalDefaults = [&]() {
        settings.setSortOrder(sortOrder());
        settings.setSortColumn(sortColumn());
        settings.setViewMode(viewMode());
        settings.setShowHidden(showHidden());
        settings.setSortFolderFirst(sortFolderFirst());
        settings.setSortCaseSensitive(sortCaseSensitive());
    };

    QVector<SchemaKey> schemaKeys;
    if (!loadDirectorySchema(schemaKeys)) {
        applyGlobalDefaults();
        return settings;
    }

    const auto directoryProfileBaseAst = [&]() {
        QHash<QString, QVariant> ast;
        ast.reserve(schemaKeys.size());
        ast.insert(QString::fromUtf8(kDirectorySchemaVersionKey), CURRENT_SCHEMA_VERSION);
        ast.insert(QStringLiteral("SortOrder"), QString::fromUtf8(sortOrderToString(sortOrder())));
        ast.insert(QStringLiteral("SortColumn"), QString::fromUtf8(sortColumnToString(sortColumn())));
        ast.insert(QStringLiteral("ViewMode"), QString::fromUtf8(viewModeToString(viewMode())));
        ast.insert(QStringLiteral("ShowHidden"), showHidden());
        ast.insert(QStringLiteral("SortFolderFirst"), sortFolderFirst());
        ast.insert(QStringLiteral("SortCaseSensitive"), sortCaseSensitive());
        ast.insert(QStringLiteral("Recursive"), false);
        return ast;
    };

    const auto readCompatibleNormalized = [&](Panel::FolderConfig& cfg, QHash<QString, QVariant>& normalizedOut) {
        if (cfg.isEmpty()) {
            return false;
        }
        QHash<QString, QVariant> ast = readFolderConfigAst(cfg, schemaKeys);
        MigrationReport migrationReport;
        if (!migrateAstForward(SettingsSurface::Directory, QString::fromUtf8(kDirectorySchemaVersionKey), ast,
                               &migrationReport)) {
            return false;
        }
        QHash<QString, QVariant> effectiveAst = directoryProfileBaseAst();
        for (auto it = ast.cbegin(); it != ast.cend(); ++it) {
            effectiveAst.insert(it.key(), it.value());
        }
        normalizedOut = normalizeAstBySchema(schemaKeys, effectiveAst);
        if (!hasSupportedSchemaVersion(normalizedOut, QString::fromUtf8(kDirectorySchemaVersionKey))) {
            return false;
        }
        lastDirectoryMigrationSourceVersion_ = migrationReport.sourceVersion;
        lastDirectoryMigrationTargetVersion_ = migrationReport.targetVersion;
        lastDirectoryMigrationActions_ = formatMigrationActions(migrationReport);
        return true;
    };

    Panel::FolderConfig cfg(path);
    QHash<QString, QVariant> effectiveNormalized;
    const bool customized = readCompatibleNormalized(cfg, effectiveNormalized);
    Panel::FilePath inheritedPath;
    if (!customized && !path.isParentOf(path)) {  // WARNING: menu://applications/ is its own parent
        inheritedPath = path.parent();
        while (inheritedPath.isValid()) {
            Panel::GErrorPtr err;
            cfg.close(err);
            cfg.open(inheritedPath);
            QHash<QString, QVariant> candidateNormalized;
            if (readCompatibleNormalized(cfg, candidateNormalized) &&
                candidateNormalized.value(QStringLiteral("Recursive")).toBool()) {
                effectiveNormalized = std::move(candidateNormalized);
                break;
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
        applyGlobalDefaults();
    }
    else {
        // either the folder is customized or it inherits settings; load folder-specific settings
        const auto value = [&](const char* key) { return effectiveNormalized.value(QString::fromUtf8(key)); };
        if (!inheritedPath.isValid()) {
            settings.setCustomized(true);
        }
        else {
            settings.seInheritedPath(inheritedPath);
        }

        settings.setSortOrder(FolderSettings::sortOrderFromString(value("SortOrder").toString()));
        settings.setSortColumn(FolderSettings::sortColumnFromString(value("SortColumn").toString()));
        settings.setViewMode(FolderSettings::viewModeFromString(value("ViewMode").toString()));
        settings.setShowHidden(value("ShowHidden").toBool());
        settings.setSortFolderFirst(value("SortFolderFirst").toBool());
        settings.setSortCaseSensitive(value("SortCaseSensitive").toBool());
        settings.setRecursive(value("Recursive").toBool());
    }
    return settings;
}

void Settings::saveFolderSettings(const Panel::FilePath& path, const FolderSettings& settings) {
    if (path) {
        // ensure that we have the libfm dir
        QString dirName = xdgUserConfigDir() + QStringLiteral("/libfm");
        QString error;
        FsQt::makeDirParents(dirName, error);  // if libfm config dir does not exist, create it

        QVector<SchemaKey> schemaKeys;
        if (!loadDirectorySchema(schemaKeys)) {
            return;
        }

        QHash<QString, QVariant> values;
        values.insert(QString::fromUtf8(kDirectorySchemaVersionKey), CURRENT_SCHEMA_VERSION);
        values.insert(QStringLiteral("SortOrder"), QString::fromUtf8(sortOrderToString(settings.sortOrder())));
        values.insert(QStringLiteral("SortColumn"), QString::fromUtf8(sortColumnToString(settings.sortColumn())));
        values.insert(QStringLiteral("ViewMode"), QString::fromUtf8(viewModeToString(settings.viewMode())));
        values.insert(QStringLiteral("ShowHidden"), settings.showHidden());
        values.insert(QStringLiteral("SortFolderFirst"), settings.sortFolderFirst());
        values.insert(QStringLiteral("SortCaseSensitive"), settings.sortCaseSensitive());
        values.insert(QStringLiteral("Recursive"), settings.recursive());

        const QHash<QString, QVariant> normalized = normalizeAstBySchema(schemaKeys, values);
        Panel::FolderConfig cfg(path);
        for (const SchemaKey& schemaKey : schemaKeys) {
            const QByteArray keyUtf8 = schemaKey.key.toUtf8();
            const QVariant normalizedValue = normalized.value(schemaKey.key, schemaKey.defaultValue);
            if (schemaKey.type == QLatin1String("bool")) {
                const bool fallbackValue = parseBoolValue(schemaKey.defaultValue, false);
                cfg.setBoolean(keyUtf8.constData(), parseBoolValue(normalizedValue, fallbackValue));
            }
            else {
                const QByteArray valueUtf8 = normalizedValue.toString().toUtf8();
                cfg.setString(keyUtf8.constData(), valueUtf8.constData());
            }
        }
    }
}

void Settings::clearFolderSettings(const Panel::FilePath& path) const {
    if (path) {
        Panel::FolderConfig cfg(path);
        cfg.purge();
    }
}

}  // namespace Oneg4FM
