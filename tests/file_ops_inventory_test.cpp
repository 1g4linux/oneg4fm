/*
 * Inventory verification for duplicated file-ops paths.
 * tests/file_ops_inventory_test.cpp
 */

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QTest>

class FileOpsInventoryTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void inventoryHasRequiredCategories();
    void inventoryEntriesMapToRealSymbols();
    void legacyNativeCategoryHasNoActiveDuplicates();
    void docsForPhaseFiveThreeExistAndAreNonEmpty();
    void hackingDocCoversLinuxSecurityModelAndContract();
    void coreContractDocCoversFieldsEventsAndErrors();
    void adapterDocCoversQtAndLibfmQtBridge();

   private:
    QString sourceRoot() const;
    QPair<QJsonObject, QString> readInventory() const;
    QPair<QString, QString> readTextFile(const QString& relPath) const;
};

QString FileOpsInventoryTest::sourceRoot() const {
#ifdef PCMANFM_SOURCE_DIR
    return QString::fromUtf8(PCMANFM_SOURCE_DIR);
#else
    return {};
#endif
}

QPair<QJsonObject, QString> FileOpsInventoryTest::readInventory() const {
    const QString root = sourceRoot();
    if (root.isEmpty()) {
        return {{}, QStringLiteral("PCMANFM_SOURCE_DIR compile definition is missing")};
    }

    const QString inventoryPath = root + QLatin1String("/docs/file-ops-duplication-inventory.json");
    QFile inventoryFile(inventoryPath);
    if (!inventoryFile.exists()) {
        return {{}, QStringLiteral("Missing inventory file: %1").arg(inventoryPath)};
    }
    if (!inventoryFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, QStringLiteral("Failed to read inventory file: %1").arg(inventoryPath)};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(inventoryFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {{}, QStringLiteral("Invalid JSON in inventory: %1").arg(parseError.errorString())};
    }
    if (!doc.isObject()) {
        return {{}, QStringLiteral("Inventory JSON root must be an object")};
    }
    return {doc.object(), {}};
}

QPair<QString, QString> FileOpsInventoryTest::readTextFile(const QString& relPath) const {
    const QString root = sourceRoot();
    if (root.isEmpty()) {
        return {{}, QStringLiteral("PCMANFM_SOURCE_DIR compile definition is missing")};
    }

    const QString absPath = root + QLatin1Char('/') + relPath;
    QFile file(absPath);
    if (!file.exists()) {
        return {{}, QStringLiteral("Missing file: %1").arg(absPath)};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, QStringLiteral("Failed to read file: %1").arg(absPath)};
    }
    return {QString::fromUtf8(file.readAll()), {}};
}

void FileOpsInventoryTest::inventoryHasRequiredCategories() {
    const auto [root, error] = readInventory();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QJsonObject categories = root.value(QStringLiteral("categories")).toObject();
    QVERIFY2(!categories.isEmpty(), "Inventory categories must not be empty");

    const QStringList requiredCategories = {
        QStringLiteral("legacy_native_copy_move_delete"),
        QStringLiteral("redundant_planners"),
        QStringLiteral("duplicate_conflict_handlers"),
    };

    for (const QString& category : requiredCategories) {
        QVERIFY2(categories.contains(category), qPrintable(QStringLiteral("Missing category: %1").arg(category)));
        const QJsonArray entries = categories.value(category).toArray();
        QVERIFY2(!entries.isEmpty(), qPrintable(QStringLiteral("Category has no entries: %1").arg(category)));
    }
}

void FileOpsInventoryTest::inventoryEntriesMapToRealSymbols() {
    const auto [root, error] = readInventory();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QJsonObject categories = root.value(QStringLiteral("categories")).toObject();
    const QString rootPath = sourceRoot();

    for (auto categoryIt = categories.begin(); categoryIt != categories.end(); ++categoryIt) {
        const QString categoryName = categoryIt.key();
        const QJsonArray entries = categoryIt.value().toArray();
        QVERIFY2(!entries.isEmpty(),
                 qPrintable(QStringLiteral("Inventory category is unexpectedly empty: %1").arg(categoryName)));

        for (int i = 0; i < entries.size(); ++i) {
            QVERIFY2(entries.at(i).isObject(),
                     qPrintable(QStringLiteral("Entry %1 in category %2 is not an object").arg(i).arg(categoryName)));

            const QJsonObject entry = entries.at(i).toObject();
            const QString relFile = entry.value(QStringLiteral("file")).toString();
            const QString symbol = entry.value(QStringLiteral("symbol")).toString();

            QVERIFY2(
                !relFile.isEmpty(),
                qPrintable(QStringLiteral("Entry %1 in category %2 has empty file path").arg(i).arg(categoryName)));
            QVERIFY2(!symbol.isEmpty(),
                     qPrintable(QStringLiteral("Entry %1 in category %2 has empty symbol").arg(i).arg(categoryName)));

            const QString absFile = rootPath + QLatin1Char('/') + relFile;
            QFile sourceFile(absFile);
            QVERIFY2(sourceFile.exists(),
                     qPrintable(QStringLiteral("Inventory file target does not exist: %1").arg(absFile)));
            QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
                     qPrintable(QStringLiteral("Failed to read inventory file target: %1").arg(absFile)));

            const QByteArray fileData = sourceFile.readAll();
            QVERIFY2(fileData.contains(symbol.toUtf8()),
                     qPrintable(QStringLiteral("Inventory symbol not found in file (%1): %2").arg(relFile, symbol)));
        }
    }
}

void FileOpsInventoryTest::legacyNativeCategoryHasNoActiveDuplicates() {
    const auto [root, error] = readInventory();
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QJsonObject categories = root.value(QStringLiteral("categories")).toObject();
    QVERIFY2(categories.contains(QStringLiteral("legacy_native_copy_move_delete")),
             "Missing legacy_native_copy_move_delete category");

    const QJsonArray entries = categories.value(QStringLiteral("legacy_native_copy_move_delete")).toArray();
    QVERIFY2(!entries.isEmpty(), "legacy_native_copy_move_delete category must not be empty");

    for (int i = 0; i < entries.size(); ++i) {
        QVERIFY2(entries.at(i).isObject(), "legacy_native_copy_move_delete entry is not an object");
        const QJsonObject entry = entries.at(i).toObject();
        const QString state = entry.value(QStringLiteral("state")).toString();
        QVERIFY2(
            state != QStringLiteral("active-duplicate"),
            qPrintable(
                QStringLiteral("legacy_native_copy_move_delete contains active duplicate entry at index %1").arg(i)));
    }
}

void FileOpsInventoryTest::docsForPhaseFiveThreeExistAndAreNonEmpty() {
    const QStringList requiredDocs = {
        QStringLiteral("HACKING.md"),
        QStringLiteral("docs/file-ops-core-contract.md"),
        QStringLiteral("docs/file-ops-adapters.md"),
    };

    for (const QString& relPath : requiredDocs) {
        const auto [content, error] = readTextFile(relPath);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(!content.trimmed().isEmpty(),
                 qPrintable(QStringLiteral("Doc is unexpectedly empty: %1").arg(relPath)));
    }
}

void FileOpsInventoryTest::hackingDocCoversLinuxSecurityModelAndContract() {
    const auto [content, error] = readTextFile(QStringLiteral("HACKING.md"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QStringList requiredTerms = {
        QStringLiteral("Linux-only"),
        QStringLiteral("openat2"),
        QStringLiteral("renameat2"),
        QStringLiteral("statx"),
        QStringLiteral("no-follow"),
        QStringLiteral("ECANCELED"),
        QStringLiteral("FileOpsContract::Request"),
    };

    for (const QString& term : requiredTerms) {
        QVERIFY2(content.contains(term, Qt::CaseSensitive),
                 qPrintable(QStringLiteral("HACKING.md is missing required term: %1").arg(term)));
    }
}

void FileOpsInventoryTest::coreContractDocCoversFieldsEventsAndErrors() {
    const auto [content, error] = readTextFile(QStringLiteral("docs/file-ops-core-contract.md"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QStringList requiredTerms = {
        QStringLiteral("Request"),
        QStringLiteral("LinuxSafetyRequirements"),
        QStringLiteral("ProgressSnapshot"),
        QStringLiteral("ConflictEvent"),
        QStringLiteral("onProgress"),
        QStringLiteral("onConflict"),
        QStringLiteral("EngineErrorCode"),
        QStringLiteral("OperationStep"),
        QStringLiteral("SafetyRequirementUnavailable"),
        QStringLiteral("Cancelled"),
    };

    for (const QString& term : requiredTerms) {
        QVERIFY2(content.contains(term, Qt::CaseSensitive),
                 qPrintable(QStringLiteral("Core contract doc is missing required term: %1").arg(term)));
    }
}

void FileOpsInventoryTest::adapterDocCoversQtAndLibfmQtBridge() {
    const auto [content, error] = readTextFile(QStringLiteral("docs/file-ops-adapters.md"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QStringList requiredTerms = {
        QStringLiteral("QtFileOps"),
        QStringLiteral("FileOpRequest"),
        QStringLiteral("FileTransferJob"),
        QStringLiteral("DeleteJob"),
        QStringLiteral("classifyPathForFileOps"),
        QStringLiteral("RoutingClass::CoreLocal"),
        QStringLiteral("RoutingClass::LegacyGio"),
        QStringLiteral("RoutingClass::Unsupported"),
    };

    for (const QString& term : requiredTerms) {
        QVERIFY2(content.contains(term, Qt::CaseSensitive),
                 qPrintable(QStringLiteral("Adapter doc is missing required term: %1").arg(term)));
    }
}

QTEST_MAIN(FileOpsInventoryTest)

#include "file_ops_inventory_test.moc"
