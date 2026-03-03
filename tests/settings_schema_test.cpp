#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QObject>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "settings.h"

#ifndef PCMANFM_SOURCE_DIR
#error "PCMANFM_SOURCE_DIR must be defined"
#endif

class TestSettingsSchema : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void schemaHasRequiredMetadata();
    void profileSchemaKeysMatchSavedSettings();
    void defaultTemplateKeysMatchProfileSchema();

   private:
    QString sourcePath(const QString& relative) const;
    QJsonObject loadSchema() const;
    QSet<QString> schemaSurfaceKeys(const QJsonObject& schema, const QString& surfaceId) const;
    QSet<QString> iniKeys(const QString& filePath) const;
};

QString TestSettingsSchema::sourcePath(const QString& relative) const {
    return QStringLiteral(PCMANFM_SOURCE_DIR) + QLatin1Char('/') + relative;
}

QJsonObject TestSettingsSchema::loadSchema() const {
    const QString path = sourcePath(QStringLiteral("config/oneg4fm/schema.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("Failed to open schema file: %1").arg(path);
        return QJsonObject();
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning().noquote() << QStringLiteral("Schema JSON parse error at offset %1: %2")
                                    .arg(parseError.offset)
                                    .arg(parseError.errorString());
        return QJsonObject();
    }
    if (!doc.isObject()) {
        qWarning("Schema root must be a JSON object");
        return QJsonObject();
    }
    return doc.object();
}

QSet<QString> TestSettingsSchema::schemaSurfaceKeys(const QJsonObject& schema, const QString& surfaceId) const {
    const QJsonArray surfaces = schema.value(QStringLiteral("surfaces")).toArray();
    for (const QJsonValue& surfaceValue : surfaces) {
        const QJsonObject surface = surfaceValue.toObject();
        if (surface.value(QStringLiteral("id")).toString() != surfaceId) {
            continue;
        }

        const QJsonArray keys = surface.value(QStringLiteral("keys")).toArray();
        QSet<QString> out;
        for (const QJsonValue& keyValue : keys) {
            const QString key = keyValue.toObject().value(QStringLiteral("key")).toString();
            if (!key.isEmpty()) {
                out.insert(key);
            }
        }
        return out;
    }

    qWarning().noquote() << QStringLiteral("Schema surface not found: %1").arg(surfaceId);
    return {};
}

QSet<QString> TestSettingsSchema::iniKeys(const QString& filePath) const {
    QSettings settings(filePath, QSettings::IniFormat);
    const QStringList keys = settings.allKeys();
    return {keys.cbegin(), keys.cend()};
}

void TestSettingsSchema::schemaHasRequiredMetadata() {
    const QJsonObject schema = loadSchema();
    QVERIFY2(!schema.isEmpty(), "Schema file must parse into a JSON object");
    const QJsonArray surfaces = schema.value(QStringLiteral("surfaces")).toArray();
    QVERIFY2(!surfaces.isEmpty(), "Schema must define at least one surface");

    const QSet<QString> validTypes = {
        QStringLiteral("bool"), QStringLiteral("int"),  QStringLiteral("string"), QStringLiteral("enum"),
        QStringLiteral("path"), QStringLiteral("list"), QStringLiteral("map"),
    };
    const QSet<QString> validStability = {
        QStringLiteral("stable"),
        QStringLiteral("experimental"),
        QStringLiteral("internal"),
    };
    const QSet<QString> validScope = {
        QStringLiteral("global"),
        QStringLiteral("per-profile"),
        QStringLiteral("per-directory"),
    };
    const QSet<QString> validSecrecy = {
        QStringLiteral("ok-to-log"),
        QStringLiteral("sensitive"),
    };

    for (const QJsonValue& surfaceValue : surfaces) {
        QVERIFY2(surfaceValue.isObject(), "Each schema surface must be an object");
        const QJsonObject surface = surfaceValue.toObject();
        const QString surfaceId = surface.value(QStringLiteral("id")).toString();
        QVERIFY2(!surfaceId.isEmpty(), "Surface id must be non-empty");

        const QString surfaceScope = surface.value(QStringLiteral("scope")).toString();
        QVERIFY2(validScope.contains(surfaceScope), "Surface scope must be one of global/per-profile/per-directory");

        const QJsonArray keys = surface.value(QStringLiteral("keys")).toArray();
        QVERIFY2(!keys.isEmpty(), "Each surface must define at least one key");

        QSet<QString> uniqueKeys;
        for (const QJsonValue& keyValue : keys) {
            QVERIFY2(keyValue.isObject(), "Each key entry must be an object");
            const QJsonObject keyObj = keyValue.toObject();

            const QString keyPath = keyObj.value(QStringLiteral("key")).toString();
            QVERIFY2(!keyPath.isEmpty(), "Key path must be non-empty");
            QVERIFY2(!uniqueKeys.contains(keyPath), "Duplicate key in schema surface");
            uniqueKeys.insert(keyPath);

            const QString type = keyObj.value(QStringLiteral("type")).toString();
            QVERIFY2(validTypes.contains(type), "Invalid type in schema key entry");

            QVERIFY2(keyObj.contains(QStringLiteral("constraints")) &&
                         keyObj.value(QStringLiteral("constraints")).isObject(),
                     "Schema key must include constraints object");
            QVERIFY2(keyObj.contains(QStringLiteral("default")), "Schema key must include default value");

            const QString stability = keyObj.value(QStringLiteral("stability")).toString();
            QVERIFY2(validStability.contains(stability), "Invalid stability value in schema key entry");

            const QString description = keyObj.value(QStringLiteral("description")).toString();
            QVERIFY2(!description.isEmpty(), "Schema key description must be non-empty");

            const QString scope = keyObj.value(QStringLiteral("scope")).toString();
            QVERIFY2(validScope.contains(scope), "Invalid scope value in schema key entry");

            const QString secrecy = keyObj.value(QStringLiteral("secrecy")).toString();
            QVERIFY2(validSecrecy.contains(secrecy), "Invalid secrecy value in schema key entry");
        }
    }
}

void TestSettingsSchema::profileSchemaKeysMatchSavedSettings() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8()));

    Oneg4FM::Settings settings;
    QVERIFY(settings.load(QStringLiteral("schema-profile")));
    QVERIFY(settings.save(QStringLiteral("schema-profile")));

    const QString settingsPath =
        settings.profileDir(QStringLiteral("schema-profile")) + QStringLiteral("/settings.conf");
    QVERIFY2(QFile::exists(settingsPath),
             qPrintable(QStringLiteral("settings.conf not written: %1").arg(settingsPath)));

    const QSet<QString> savedKeys = iniKeys(settingsPath);
    const QSet<QString> schemaKeys = schemaSurfaceKeys(loadSchema(), QStringLiteral("profile.settings.conf"));
    QVERIFY2(!schemaKeys.isEmpty(), "Profile schema key set must not be empty");
    QCOMPARE(savedKeys, schemaKeys);
}

void TestSettingsSchema::defaultTemplateKeysMatchProfileSchema() {
    const QString templatePath = sourcePath(QStringLiteral("config/oneg4fm/default/settings.conf.in"));
    QVERIFY2(QFile::exists(templatePath), qPrintable(QStringLiteral("Template not found: %1").arg(templatePath)));

    const QSet<QString> templateKeys = iniKeys(templatePath);
    const QSet<QString> schemaKeys = schemaSurfaceKeys(loadSchema(), QStringLiteral("profile.settings.conf"));
    QVERIFY2(!schemaKeys.isEmpty(), "Profile schema key set must not be empty");
    QCOMPARE(templateKeys, schemaKeys);
}

QTEST_MAIN(TestSettingsSchema)
#include "settings_schema_test.moc"
