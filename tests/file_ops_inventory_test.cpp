/*
 * Inventory verification for duplicated file-ops paths.
 * tests/file_ops_inventory_test.cpp
 */

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

class FileOpsInventoryTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void inventoryHasRequiredCategories();
    void inventoryEntriesMapToRealSymbols();
    void legacyNativeCategoryHasNoActiveDuplicates();
    void legacyIdentityPatternMatchesConfiguredTokenSet();
    void projectOwnedPathsRejectLegacyIdentityTokens();
    void docsForPhaseFiveThreeExistAndAreNonEmpty();
    void dbusCompatibilityAdrExistsAndSpecifiesPolicy();
    void dbusBuildArtifactsEnforceHardCutServiceIdentity();
    void compatibilityAliasAllowlistMatchesDbusPolicy();
    void installArtifactsUseOneg4fmIdentity();
    void installedArtifactsRejectLegacyIdentityTokens();
    void userFacingIdentitySweepUsesExplicitCompatibilityAllowlist();
    void applicationUsesTypedMainWindowChecks();
    void applicationLifecycleAndWindowOrchestrationAreSplitIntoDedicatedUnits();
    void applicationExposesSettingsDiagnosticsCliActions();
    void shutdownOrderingIsExplicitInLifecycleUnit();
    void mainWindowUiKeysAndMimeTypesAreCentralized();
    void hackingDocCoversLinuxSecurityModelAndContract();
    void coreContractDocCoversFieldsEventsAndErrors();
    void adapterDocCoversQtAndLibfmQtBridge();
    void libfmQtCoreRoutedAdaptersAvoidPlannerRetryProbeLogic();

   private:
    QString sourceRoot() const;
    QString binaryRoot() const;
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

QString FileOpsInventoryTest::binaryRoot() const {
#ifdef ONEG4FM_BINARY_DIR
    return QString::fromUtf8(ONEG4FM_BINARY_DIR);
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

void FileOpsInventoryTest::legacyIdentityPatternMatchesConfiguredTokenSet() {
    const QRegularExpression pattern(QStringLiteral("(pcmanfm|PCManFM|org\\.pcmanfm)"));
    QVERIFY2(pattern.isValid(), "Legacy identity pattern must be a valid regular expression");
    QVERIFY2(pattern.match(QStringLiteral("pcmanfm")).hasMatch(),
             "Legacy identity pattern must match lowercase pcmanfm");
    QVERIFY2(pattern.match(QStringLiteral("PCManFM")).hasMatch(),
             "Legacy identity pattern must match camel-case PCManFM");
    QVERIFY2(pattern.match(QStringLiteral("org.pcmanfm")).hasMatch(), "Legacy identity pattern must match org.pcmanfm");
    QVERIFY2(!pattern.match(QStringLiteral("PCMANFM")).hasMatch(),
             "Legacy identity pattern must not over-match unrelated legacy macro casing");
    QVERIFY2(!pattern.match(QStringLiteral("org.oneg4fm")).hasMatch(),
             "Legacy identity pattern must not match current oneg4fm identifiers");
}

void FileOpsInventoryTest::projectOwnedPathsRejectLegacyIdentityTokens() {
    const QString root = sourceRoot();
    QVERIFY2(!root.isEmpty(), "PCMANFM_SOURCE_DIR compile definition is missing");
    const QDir rootDir(root);

    const QStringList scannedDirectories = {
        QStringLiteral("cmake"), QStringLiteral("config"),       QStringLiteral("oneg4fm"),
        QStringLiteral("src"),   QStringLiteral("libfm-qt/src"),
    };
    const QStringList scannedNameFilters = {
        QStringLiteral("*.c"),          QStringLiteral("*.cc"),           QStringLiteral("*.cpp"),
        QStringLiteral("*.h"),          QStringLiteral("*.hh"),           QStringLiteral("*.hpp"),
        QStringLiteral("*.ui"),         QStringLiteral("*.cmake"),        QStringLiteral("*.desktop"),
        QStringLiteral("*.desktop.in"), QStringLiteral("*.service"),      QStringLiteral("*.service.in"),
        QStringLiteral("*.xml"),        QStringLiteral("*.json"),         QStringLiteral("*.conf"),
        QStringLiteral("*.in"),         QStringLiteral("CMakeLists.txt"),
    };
    const QStringList scannedTopLevelFiles = {
        QStringLiteral("CMakeLists.txt"),
    };

    QSet<QString> scannedFiles;
    const auto addMatchingFiles = [&](const QString& relDir) {
        QDirIterator it(root + QLatin1Char('/') + relDir, scannedNameFilters, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absPath = it.next();
            const QString relPath = rootDir.relativeFilePath(absPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
            scannedFiles.insert(relPath);
        }
    };

    for (const QString& relDir : scannedDirectories) {
        addMatchingFiles(relDir);
    }
    for (const QString& relPath : scannedTopLevelFiles) {
        scannedFiles.insert(relPath);
    }

    QVERIFY2(!scannedFiles.isEmpty(), "Project-owned identity gate scanned zero files");

    const QRegularExpression legacyIdPattern(QStringLiteral("(pcmanfm|PCManFM|org\\.pcmanfm)"));
    QStringList files = scannedFiles.values();
    files.sort(Qt::CaseSensitive);

    QStringList violations;
    for (const QString& relPath : files) {
        const auto [content, error] = readTextFile(relPath);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        if (legacyIdPattern.match(relPath).hasMatch() || legacyIdPattern.match(content).hasMatch()) {
            violations.append(relPath);
        }
    }

    QVERIFY2(violations.isEmpty(), qPrintable(QStringLiteral("Legacy identity token found in project-owned paths:\n%1")
                                                  .arg(violations.join(QLatin1Char('\n')))));
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

void FileOpsInventoryTest::dbusCompatibilityAdrExistsAndSpecifiesPolicy() {
    const auto [content, error] = readTextFile(QStringLiteral("docs/adr/0001-dbus-compat-policy.md"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QStringList requiredTerms = {
        QStringLiteral("Decision"),
        QStringLiteral("hard cut"),
        QStringLiteral("org.oneg4fm.oneg4fm"),
        QStringLiteral("org.oneg4fm.Application"),
        QStringLiteral("org.pcmanfm"),
        QStringLiteral("no compatibility alias"),
    };

    for (const QString& term : requiredTerms) {
        QVERIFY2(content.contains(term, Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("DBus ADR is missing required term: %1").arg(term)));
    }
}

void FileOpsInventoryTest::dbusBuildArtifactsEnforceHardCutServiceIdentity() {
    const auto [cmakeContent, cmakeError] = readTextFile(QStringLiteral("oneg4fm/CMakeLists.txt"));
    QVERIFY2(cmakeError.isEmpty(), qPrintable(cmakeError));

    const QStringList requiredCMakeTerms = {
        QStringLiteral("ONEG4FM_DBUS_APP_SERVICE"),
        QStringLiteral("ONEG4FM_DBUS_APP_INTERFACE"),
        QStringLiteral("org.oneg4fm.oneg4fm.service.in"),
        QStringLiteral("dbus-1/services"),
    };
    for (const QString& term : requiredCMakeTerms) {
        QVERIFY2(cmakeContent.contains(term, Qt::CaseSensitive),
                 qPrintable(QStringLiteral("oneg4fm/CMakeLists.txt missing DBus enforcement term: %1").arg(term)));
    }

    const auto [serviceContent, serviceError] = readTextFile(QStringLiteral("oneg4fm/org.oneg4fm.oneg4fm.service.in"));
    QVERIFY2(serviceError.isEmpty(), qPrintable(serviceError));
    QVERIFY2(serviceContent.contains(QStringLiteral("[D-BUS Service]"), Qt::CaseSensitive),
             "DBus service template must declare [D-BUS Service]");
    QVERIFY2(serviceContent.contains(QStringLiteral("Name=@ONEG4FM_DBUS_APP_SERVICE@"), Qt::CaseSensitive),
             "DBus service template must bind Name to ONEG4FM_DBUS_APP_SERVICE");
    QVERIFY2(serviceContent.contains(QStringLiteral("Exec=@CMAKE_INSTALL_FULL_BINDIR@/oneg4fm"), Qt::CaseSensitive),
             "DBus service template must install-activate the oneg4fm binary");
    QVERIFY2(!serviceContent.contains(QStringLiteral("org.pcmanfm"), Qt::CaseInsensitive),
             "DBus service template must not define legacy org.pcmanfm aliases");

    const auto [appContent, appError] = readTextFile(QStringLiteral("oneg4fm/application.cpp"));
    QVERIFY2(appError.isEmpty(), qPrintable(appError));
    QVERIFY2(appContent.contains(QStringLiteral("ONEG4FM_DBUS_APP_SERVICE"), Qt::CaseSensitive),
             "application.cpp must consume ONEG4FM_DBUS_APP_SERVICE");
    QVERIFY2(appContent.contains(QStringLiteral("ONEG4FM_DBUS_APP_INTERFACE"), Qt::CaseSensitive),
             "application.cpp must consume ONEG4FM_DBUS_APP_INTERFACE");
    QVERIFY2(!appContent.contains(QStringLiteral("org.pcmanfm"), Qt::CaseInsensitive),
             "application.cpp must not reference legacy org.pcmanfm identifiers");
}

void FileOpsInventoryTest::compatibilityAliasAllowlistMatchesDbusPolicy() {
    const auto [adrContent, adrError] = readTextFile(QStringLiteral("docs/adr/0001-dbus-compat-policy.md"));
    QVERIFY2(adrError.isEmpty(), qPrintable(adrError));

    const auto [allowlistContent, allowlistError] =
        readTextFile(QStringLiteral("docs/compatibility-alias-allowlist.txt"));
    QVERIFY2(allowlistError.isEmpty(), qPrintable(allowlistError));

    QStringList entries;
    const QStringList lines = allowlistContent.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        entries.append(line);
    }

    const bool hardCutPolicy = adrContent.contains(QStringLiteral("hard cut"), Qt::CaseInsensitive) &&
                               adrContent.contains(QStringLiteral("no compatibility alias"), Qt::CaseInsensitive);

    if (hardCutPolicy) {
        QVERIFY2(entries.isEmpty(), "compatibility-alias-allowlist.txt must be empty under hard-cut no-alias policy");
        return;
    }

    QVERIFY2(!entries.isEmpty(), "Temporary-alias policy requires compatibility-alias-allowlist.txt entries");

    for (int i = 0; i < entries.size(); ++i) {
        const QStringList parts = entries.at(i).split(QLatin1Char('|'));
        QVERIFY2(parts.size() == 4,
                 qPrintable(QStringLiteral("Invalid compatibility alias allowlist entry at line %1").arg(i + 1)));

        const QString legacyIdentifier = parts.at(0).trimmed();
        const QString owner = parts.at(1).trimmed();
        const QString removalDeadline = parts.at(2).trimmed();
        const QString reason = parts.at(3).trimmed();

        QVERIFY2(!legacyIdentifier.isEmpty(),
                 qPrintable(QStringLiteral("Compatibility alias entry %1 has empty identifier").arg(i + 1)));
        QVERIFY2(!owner.isEmpty(),
                 qPrintable(QStringLiteral("Compatibility alias entry %1 has empty owner").arg(i + 1)));
        QVERIFY2(!removalDeadline.isEmpty(),
                 qPrintable(QStringLiteral("Compatibility alias entry %1 has empty removal deadline").arg(i + 1)));
        QVERIFY2(!reason.isEmpty(),
                 qPrintable(QStringLiteral("Compatibility alias entry %1 has empty reason").arg(i + 1)));
    }
}

void FileOpsInventoryTest::installArtifactsUseOneg4fmIdentity() {
    const QString buildRoot = binaryRoot();
    QVERIFY2(!buildRoot.isEmpty(), "ONEG4FM_BINARY_DIR compile definition is missing");

    QTemporaryDir installStagingRoot;
    QVERIFY2(installStagingRoot.isValid(), "Failed to create temporary install staging root");

    QProcess installer;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DESTDIR"), installStagingRoot.path());
    installer.setProcessEnvironment(env);
    installer.setProgram(QStringLiteral("cmake"));
    installer.setArguments({QStringLiteral("--install"), buildRoot});
    installer.start();

    QVERIFY2(installer.waitForFinished(120000), "cmake --install timed out");
    const QString installStdout = QString::fromUtf8(installer.readAllStandardOutput());
    const QString installStderr = QString::fromUtf8(installer.readAllStandardError());
    QVERIFY2(installer.exitStatus() == QProcess::NormalExit,
             qPrintable(QStringLiteral("cmake --install did not exit normally\nstdout:\n%1\nstderr:\n%2")
                            .arg(installStdout, installStderr)));
    QVERIFY2(installer.exitCode() == 0,
             qPrintable(
                 QStringLiteral("cmake --install failed\nstdout:\n%1\nstderr:\n%2").arg(installStdout, installStderr)));

    const QRegularExpression legacyIdPattern(QStringLiteral("(pcmanfm|org\\.pcmanfm)"),
                                             QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression auditedArtifactPathPattern(
        QStringLiteral("/(applications/.*\\.desktop|appstream/.+\\.(metainfo\\.xml|appdata\\.xml)|"
                       "icons/.+\\.(svg|png|xpm)|mime/(packages/)?[^/]+\\.xml|"
                       "glib-2\\.0/schemas/.+\\.xml|dbus-1/services/.+\\.service)$"),
        QRegularExpression::CaseInsensitiveOption);

    const QDir stagingRoot(installStagingRoot.path());
    bool foundDesktop = false;
    bool foundService = false;
    bool foundIcon = false;
    bool foundMimePackage = false;
    int installedFileCount = 0;

    QDirIterator it(installStagingRoot.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absPath = it.next();
        ++installedFileCount;
        const QString relPath = stagingRoot.relativeFilePath(absPath).replace(QLatin1Char('\\'), QLatin1Char('/'));

        QVERIFY2(!legacyIdPattern.match(relPath).hasMatch(),
                 qPrintable(QStringLiteral("Installed path contains legacy identifier: %1").arg(relPath)));

        if (relPath.endsWith(QStringLiteral("/share/applications/oneg4fm.desktop"), Qt::CaseInsensitive)) {
            foundDesktop = true;
        }
        else if (relPath.endsWith(QStringLiteral("/share/dbus-1/services/org.oneg4fm.oneg4fm.service"),
                                  Qt::CaseInsensitive)) {
            foundService = true;
        }
        else if (relPath.endsWith(QStringLiteral("/share/icons/hicolor/scalable/apps/oneg4fm.svg"),
                                  Qt::CaseInsensitive)) {
            foundIcon = true;
        }
        else if (relPath.endsWith(QStringLiteral("/share/mime/packages/libfm-qt6-mimetypes.xml"),
                                  Qt::CaseInsensitive)) {
            foundMimePackage = true;
        }

        if (!auditedArtifactPathPattern.match(relPath).hasMatch()) {
            continue;
        }

        QFile artifact(absPath);
        QVERIFY2(artifact.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("Failed to read installed artifact: %1").arg(relPath)));
        const QString content = QString::fromUtf8(artifact.readAll());
        QVERIFY2(!legacyIdPattern.match(content).hasMatch(),
                 qPrintable(QStringLiteral("Installed artifact contains legacy identifier: %1").arg(relPath)));
    }

    QVERIFY2(installedFileCount > 0, "Install staging tree is empty");
    QVERIFY2(foundDesktop, "Missing installed desktop artifact: share/applications/oneg4fm.desktop");
    QVERIFY2(foundService, "Missing installed service artifact: share/dbus-1/services/org.oneg4fm.oneg4fm.service");
    QVERIFY2(foundIcon, "Missing installed icon artifact: share/icons/hicolor/scalable/apps/oneg4fm.svg");
    QVERIFY2(foundMimePackage, "Missing installed MIME artifact: share/mime/packages/libfm-qt6-mimetypes.xml");
}

void FileOpsInventoryTest::installedArtifactsRejectLegacyIdentityTokens() {
    const QString buildRoot = binaryRoot();
    QVERIFY2(!buildRoot.isEmpty(), "ONEG4FM_BINARY_DIR compile definition is missing");

    QTemporaryDir installStagingRoot;
    QVERIFY2(installStagingRoot.isValid(), "Failed to create temporary install staging root");

    QProcess installer;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DESTDIR"), installStagingRoot.path());
    installer.setProcessEnvironment(env);
    installer.setProgram(QStringLiteral("cmake"));
    installer.setArguments({QStringLiteral("--install"), buildRoot});
    installer.start();

    QVERIFY2(installer.waitForFinished(120000), "cmake --install timed out");
    const QString installStdout = QString::fromUtf8(installer.readAllStandardOutput());
    const QString installStderr = QString::fromUtf8(installer.readAllStandardError());
    QVERIFY2(installer.exitStatus() == QProcess::NormalExit,
             qPrintable(QStringLiteral("cmake --install did not exit normally\nstdout:\n%1\nstderr:\n%2")
                            .arg(installStdout, installStderr)));
    QVERIFY2(installer.exitCode() == 0,
             qPrintable(
                 QStringLiteral("cmake --install failed\nstdout:\n%1\nstderr:\n%2").arg(installStdout, installStderr)));

    const QRegularExpression legacyIdPattern(QStringLiteral("(pcmanfm|PCManFM|org\\.pcmanfm)"));
    const QRegularExpression textArtifactPathPattern(
        QStringLiteral("\\.(desktop|service|xml|conf|ini|json|txt|svg|xpm)$"),
        QRegularExpression::CaseInsensitiveOption);

    const QDir stagingRoot(installStagingRoot.path());
    int installedFileCount = 0;
    int scannedTextArtifactCount = 0;
    QStringList pathViolations;
    QStringList contentViolations;

    QDirIterator it(installStagingRoot.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absPath = it.next();
        ++installedFileCount;
        const QString relPath = stagingRoot.relativeFilePath(absPath).replace(QLatin1Char('\\'), QLatin1Char('/'));

        if (legacyIdPattern.match(relPath).hasMatch()) {
            pathViolations.append(relPath);
        }

        if (!textArtifactPathPattern.match(relPath).hasMatch()) {
            continue;
        }
        ++scannedTextArtifactCount;

        QFile artifact(absPath);
        QVERIFY2(artifact.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("Failed to read installed artifact: %1").arg(relPath)));
        const QString content = QString::fromUtf8(artifact.readAll());
        if (legacyIdPattern.match(content).hasMatch()) {
            contentViolations.append(relPath);
        }
    }

    QVERIFY2(installedFileCount > 0, "Install staging tree is empty");
    QVERIFY2(scannedTextArtifactCount > 0, "Install staging tree has no text artifacts to audit");
    QVERIFY2(pathViolations.isEmpty(),
             qPrintable(QStringLiteral("Installed artifact path contains legacy identifier:\n%1")
                            .arg(pathViolations.join(QLatin1Char('\n')))));
    QVERIFY2(contentViolations.isEmpty(),
             qPrintable(QStringLiteral("Installed artifact content contains legacy identifier:\n%1")
                            .arg(contentViolations.join(QLatin1Char('\n')))));
}

void FileOpsInventoryTest::userFacingIdentitySweepUsesExplicitCompatibilityAllowlist() {
    const auto [allowlistContent, allowlistError] =
        readTextFile(QStringLiteral("docs/legacy-identifier-allowlist.txt"));
    QVERIFY2(allowlistError.isEmpty(), qPrintable(allowlistError));

    QSet<QString> allowlistedFiles;
    const QStringList allowlistLines = allowlistContent.split(QLatin1Char('\n'));
    for (int i = 0; i < allowlistLines.size(); ++i) {
        const QString line = allowlistLines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const QStringList parts = line.split(QLatin1Char('|'));
        QVERIFY2(parts.size() == 2,
                 qPrintable(QStringLiteral("Invalid allowlist entry at line %1 in docs/legacy-identifier-allowlist.txt")
                                .arg(i + 1)));

        const QString relPath = parts.at(0).trimmed();
        const QString reason = parts.at(1).trimmed();
        QVERIFY2(!relPath.isEmpty(),
                 qPrintable(QStringLiteral("Allowlist entry at line %1 has empty path").arg(i + 1)));
        QVERIFY2(!reason.isEmpty(),
                 qPrintable(QStringLiteral("Allowlist entry at line %1 has empty reason").arg(i + 1)));
        QVERIFY2(!allowlistedFiles.contains(relPath),
                 qPrintable(QStringLiteral("Duplicate allowlist path at line %1: %2").arg(i + 1).arg(relPath)));

        const auto [content, error] = readTextFile(relPath);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        Q_UNUSED(content);

        allowlistedFiles.insert(relPath);
    }
    QVERIFY2(!allowlistedFiles.isEmpty(), "Legacy identifier allowlist must not be empty");

    const QString root = sourceRoot();
    QVERIFY2(!root.isEmpty(), "PCMANFM_SOURCE_DIR compile definition is missing");
    const QDir rootDir(root);

    QSet<QString> scannedFiles;
    scannedFiles.insert(QStringLiteral("README.md"));

    const auto addMatchingFiles = [&](const QString& relDir, const QStringList& nameFilters) {
        QDirIterator it(root + QLatin1Char('/') + relDir, nameFilters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absPath = it.next();
            const QString relPath = rootDir.relativeFilePath(absPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
            scannedFiles.insert(relPath);
        }
    };

    addMatchingFiles(QStringLiteral("docs"), {QStringLiteral("*.md")});
    addMatchingFiles(QStringLiteral("oneg4fm"), {
                                                    QStringLiteral("*.ui"),
                                                    QStringLiteral("*.desktop.in"),
                                                    QStringLiteral("*.desktop"),
                                                    QStringLiteral("*.ts"),
                                                    QStringLiteral("*.po"),
                                                    QStringLiteral("*.pot"),
                                                });

    for (const QString& relPath : allowlistedFiles) {
        QVERIFY2(scannedFiles.contains(relPath),
                 qPrintable(QStringLiteral("Allowlisted path is outside the identity sweep scope: %1").arg(relPath)));
    }

    const QRegularExpression legacyIdPattern(QStringLiteral("(pcmanfm|org\\.pcmanfm)"),
                                             QRegularExpression::CaseInsensitiveOption);

    QStringList files = scannedFiles.values();
    files.sort(Qt::CaseSensitive);
    for (const QString& relPath : files) {
        const auto [content, error] = readTextFile(relPath);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const bool hasLegacyId = legacyIdPattern.match(content).hasMatch();

        if (allowlistedFiles.contains(relPath)) {
            QVERIFY2(
                hasLegacyId,
                qPrintable(
                    QStringLiteral("Allowlisted file must contain a legacy identifier reference: %1").arg(relPath)));
            continue;
        }

        QVERIFY2(!hasLegacyId,
                 qPrintable(QStringLiteral("User-facing file contains a legacy identifier: %1").arg(relPath)));
    }
}

void FileOpsInventoryTest::applicationUsesTypedMainWindowChecks() {
    const auto [content, error] = readTextFile(QStringLiteral("oneg4fm/application.cpp"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY2(!content.contains(QStringLiteral("inherits(\"Oneg4FM::MainWindow\")"), Qt::CaseSensitive),
             "application.cpp must not use inherits() for MainWindow checks");

    const auto [windowContent, windowError] =
        readTextFile(QStringLiteral("oneg4fm/application_window_orchestration.cpp"));
    QVERIFY2(windowError.isEmpty(), qPrintable(windowError));
    QVERIFY2(!windowContent.contains(QStringLiteral("inherits(\"Oneg4FM::MainWindow\")"), Qt::CaseSensitive),
             "application_window_orchestration.cpp must use typed MainWindow checks instead of inherits()");
    QVERIFY2(windowContent.contains(QStringLiteral("qobject_cast<MainWindow*>("), Qt::CaseSensitive),
             "application_window_orchestration.cpp must use qobject_cast<MainWindow*> for typed window checks");
}

void FileOpsInventoryTest::applicationLifecycleAndWindowOrchestrationAreSplitIntoDedicatedUnits() {
    const auto [applicationContent, applicationError] = readTextFile(QStringLiteral("oneg4fm/application.cpp"));
    QVERIFY2(applicationError.isEmpty(), qPrintable(applicationError));

    const QStringList forbiddenInApplicationCpp = {
        QStringLiteral("void Application::init("),
        QStringLiteral("int Application::exec("),
        QStringLiteral("void Application::findFiles("),
        QStringLiteral("void Application::launchFiles("),
        QStringLiteral("void Application::onSigtermNotified("),
    };
    for (const QString& signature : forbiddenInApplicationCpp) {
        QVERIFY2(!applicationContent.contains(signature, Qt::CaseSensitive),
                 qPrintable(QStringLiteral("application.cpp should not define %1 after decomposition").arg(signature)));
    }

    const auto [lifecycleContent, lifecycleError] = readTextFile(QStringLiteral("oneg4fm/application_lifecycle.cpp"));
    QVERIFY2(lifecycleError.isEmpty(), qPrintable(lifecycleError));
    const QStringList requiredLifecycleSignatures = {
        QStringLiteral("void Application::initWatch("),
        QStringLiteral("void Application::init("),
        QStringLiteral("int Application::exec("),
        QStringLiteral("void Application::onAboutToQuit("),
        QStringLiteral("void Application::onSigtermNotified("),
    };
    for (const QString& signature : requiredLifecycleSignatures) {
        QVERIFY2(lifecycleContent.contains(signature, Qt::CaseSensitive),
                 qPrintable(QStringLiteral("application_lifecycle.cpp missing signature: %1").arg(signature)));
    }

    const auto [windowContent, windowError] =
        readTextFile(QStringLiteral("oneg4fm/application_window_orchestration.cpp"));
    QVERIFY2(windowError.isEmpty(), qPrintable(windowError));
    const QStringList requiredWindowSignatures = {
        QStringLiteral("void Application::findFiles("),
        QStringLiteral("void Application::launchFiles("),
        QStringLiteral("void Application::ShowItems("),
        QStringLiteral("void Application::updateFromSettings("),
    };
    for (const QString& signature : requiredWindowSignatures) {
        QVERIFY2(
            windowContent.contains(signature, Qt::CaseSensitive),
            qPrintable(QStringLiteral("application_window_orchestration.cpp missing signature: %1").arg(signature)));
    }
}

void FileOpsInventoryTest::applicationExposesSettingsDiagnosticsCliActions() {
    const auto [applicationContent, applicationError] = readTextFile(QStringLiteral("oneg4fm/application.cpp"));
    QVERIFY2(applicationError.isEmpty(), qPrintable(applicationError));

    QVERIFY2(applicationContent.contains(QStringLiteral("validate-settings"), Qt::CaseSensitive),
             "application.cpp must register --validate-settings");
    QVERIFY2(applicationContent.contains(QStringLiteral("dump-settings"), Qt::CaseSensitive),
             "application.cpp must register --dump-settings");
    QVERIFY2(applicationContent.contains(QStringLiteral("lastProfileDiagnosticsReportJson"), Qt::CaseSensitive),
             "application.cpp must print structured settings diagnostics reports");
    QVERIFY2(applicationContent.contains(QStringLiteral("dumpNormalizedEffectiveProfileSettings"), Qt::CaseSensitive),
             "application.cpp must support normalized settings dump output");

    const auto [lifecycleContent, lifecycleError] = readTextFile(QStringLiteral("oneg4fm/application_lifecycle.cpp"));
    QVERIFY2(lifecycleError.isEmpty(), qPrintable(lifecycleError));
    QVERIFY2(lifecycleContent.contains(QStringLiteral("return startupExitCode_;"), Qt::CaseSensitive),
             "application_lifecycle.cpp must return diagnostics exit status from Application::exec()");

    const auto [headerContent, headerError] = readTextFile(QStringLiteral("oneg4fm/application.h"));
    QVERIFY2(headerError.isEmpty(), qPrintable(headerError));
    QVERIFY2(headerContent.contains(QStringLiteral("startupExitCode_"), Qt::CaseSensitive),
             "application.h must store startup exit status for diagnostics actions");
}

void FileOpsInventoryTest::shutdownOrderingIsExplicitInLifecycleUnit() {
    const auto [content, error] = readTextFile(QStringLiteral("oneg4fm/application_lifecycle.cpp"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const int shutdownFnPos = content.indexOf(QStringLiteral("void Application::performShutdownSequence()"));
    QVERIFY2(shutdownFnPos >= 0, "application_lifecycle.cpp must define performShutdownSequence()");

    const int savePos = content.indexOf(QStringLiteral("settings_.save();"), shutdownFnPos);
    const int stopWatcherPos = content.indexOf(QStringLiteral("stopUserDirsWatcher();"), shutdownFnPos);
    const int stopDbusPos = content.indexOf(QStringLiteral("stopPrimaryDbusServices();"), shutdownFnPos);

    QVERIFY2(savePos >= 0, "performShutdownSequence must persist settings");
    QVERIFY2(stopWatcherPos >= 0, "performShutdownSequence must stop user-dir watchers");
    QVERIFY2(stopDbusPos >= 0, "performShutdownSequence must stop DBus services");
    QVERIFY2(savePos < stopWatcherPos && stopWatcherPos < stopDbusPos,
             "performShutdownSequence must enforce save -> stop watcher -> stop DBus ordering");

    const int aboutToQuitPos = content.indexOf(QStringLiteral("void Application::onAboutToQuit()"));
    QVERIFY2(aboutToQuitPos >= 0, "application_lifecycle.cpp must define onAboutToQuit()");
    const int invokeShutdownPos = content.indexOf(QStringLiteral("performShutdownSequence();"), aboutToQuitPos);
    QVERIFY2(invokeShutdownPos >= 0, "onAboutToQuit must invoke performShutdownSequence()");
}

void FileOpsInventoryTest::mainWindowUiKeysAndMimeTypesAreCentralized() {
    const auto [constantsContent, constantsError] = readTextFile(QStringLiteral("oneg4fm/mainwindow_ui_constants.h"));
    QVERIFY2(constantsError.isEmpty(), qPrintable(constantsError));
    QVERIFY2(constantsContent.contains(QStringLiteral("kTabMimeType"), Qt::CaseSensitive),
             "mainwindow_ui_constants.h must define kTabMimeType");
    QVERIFY2(constantsContent.contains(QStringLiteral("application/oneg4fm-tab"), Qt::CaseSensitive),
             "mainwindow_ui_constants.h must define the tab drag MIME type");
    QVERIFY2(constantsContent.contains(QStringLiteral("kBookmarkActionProperty"), Qt::CaseSensitive),
             "mainwindow_ui_constants.h must define kBookmarkActionProperty");
    QVERIFY2(constantsContent.contains(QStringLiteral("oneg4fm_bookmark"), Qt::CaseSensitive),
             "mainwindow_ui_constants.h must define the bookmark action property key");
    QVERIFY2(constantsContent.contains(QStringLiteral("kTabDroppedProperty"), Qt::CaseSensitive),
             "mainwindow_ui_constants.h must define kTabDroppedProperty");
    QVERIFY2(constantsContent.contains(QStringLiteral("_oneg4fm_tab_dropped"), Qt::CaseSensitive),
             "mainwindow_ui_constants.h must define the tab-drop property key");

    const auto [dragDropContent, dragDropError] = readTextFile(QStringLiteral("oneg4fm/mainwindow_dragdrop.cpp"));
    QVERIFY2(dragDropError.isEmpty(), qPrintable(dragDropError));
    QVERIFY2(dragDropContent.contains(QStringLiteral("UiConstants::kTabMimeType"), Qt::CaseSensitive),
             "mainwindow_dragdrop.cpp must use UiConstants::kTabMimeType");
    QVERIFY2(!dragDropContent.contains(QStringLiteral("\"application/oneg4fm-tab\""), Qt::CaseSensitive),
             "mainwindow_dragdrop.cpp must not hardcode the tab MIME type");

    const auto [tabbarContent, tabbarError] = readTextFile(QStringLiteral("oneg4fm/tabbar.cpp"));
    QVERIFY2(tabbarError.isEmpty(), qPrintable(tabbarError));
    QVERIFY2(tabbarContent.contains(QStringLiteral("UiConstants::kTabMimeType"), Qt::CaseSensitive),
             "tabbar.cpp must use UiConstants::kTabMimeType");
    QVERIFY2(!tabbarContent.contains(QStringLiteral("\"application/oneg4fm-tab\""), Qt::CaseSensitive),
             "tabbar.cpp must not hardcode the tab MIME type");
    QVERIFY2(tabbarContent.contains(QStringLiteral("UiConstants::kTabDroppedProperty"), Qt::CaseSensitive),
             "tabbar.cpp must use UiConstants::kTabDroppedProperty");

    const auto [bookmarksContent, bookmarksError] = readTextFile(QStringLiteral("oneg4fm/mainwindow_bookmarks.cpp"));
    QVERIFY2(bookmarksError.isEmpty(), qPrintable(bookmarksError));
    QVERIFY2(bookmarksContent.contains(QStringLiteral("UiConstants::kBookmarkActionProperty"), Qt::CaseSensitive),
             "mainwindow_bookmarks.cpp must use UiConstants::kBookmarkActionProperty");
    QVERIFY2(!bookmarksContent.contains(QStringLiteral("\"oneg4fm_bookmark\""), Qt::CaseSensitive),
             "mainwindow_bookmarks.cpp must not hardcode bookmark property keys");
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

void FileOpsInventoryTest::libfmQtCoreRoutedAdaptersAvoidPlannerRetryProbeLogic() {
    const auto [transferContent, transferError] = readTextFile(QStringLiteral("libfm-qt/src/core/filetransferjob.cpp"));
    QVERIFY2(transferError.isEmpty(), qPrintable(transferError));
    const int transferExecPos = transferContent.indexOf(QStringLiteral("void FileTransferJob::exec()"));
    QVERIFY2(transferExecPos >= 0, "filetransferjob.cpp must define FileTransferJob::exec()");
    QVERIFY2(!transferContent.contains(QStringLiteral("#include \"totalsizejob.h\""), Qt::CaseSensitive),
             "FileTransferJob must not include TotalSizeJob pre-scan helpers");
    const int transferPlannerPos = transferContent.indexOf(QStringLiteral("TotalSizeJob"), transferExecPos);
    QVERIFY2(transferPlannerPos < 0, "FileTransferJob must not run adapter pre-scan planner logic in exec()");

    const int transferCoreRunnerPos =
        transferContent.indexOf(QStringLiteral("auto runCoreRoutedPath ="), transferExecPos);
    QVERIFY2(transferCoreRunnerPos >= 0, "FileTransferJob core-routed runner must be present");
    const int transferCoreIfPos =
        transferContent.lastIndexOf(QStringLiteral("#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT"), transferCoreRunnerPos);
    const int transferCoreEndPos = transferContent.indexOf(QStringLiteral("#endif"), transferCoreRunnerPos);
    QVERIFY2(transferCoreIfPos >= 0 && transferCoreEndPos > transferCoreIfPos,
             "FileTransferJob core-routed section must be present");
    const QString transferCoreBlock = transferContent.mid(transferCoreIfPos, transferCoreEndPos - transferCoreIfPos);
    QVERIFY2(!transferCoreBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must not implement adapter retry loops");
    QVERIFY2(!transferCoreBlock.contains(QStringLiteral("renameRequest"), Qt::CaseSensitive),
             "FileTransferJob core-routed conflict handling must not duplicate rename retry policy");
    QVERIFY2(!transferCoreBlock.contains(QStringLiteral("queryInfoOrFallback"), Qt::CaseSensitive),
             "FileTransferJob core-routed conflict handling must avoid filesystem metadata probing helpers");
    QVERIFY2(!transferCoreBlock.contains(QStringLiteral("g_file_query_info"), Qt::CaseSensitive),
             "FileTransferJob core-routed conflict handling must not query filesystem metadata");
    QVERIFY2(transferCoreBlock.contains(QStringLiteral("case FileOperationJob::RENAME"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must map rename prompt choices");
    QVERIFY2(transferCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::Rename"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must pass rename semantics to core");
    QVERIFY2(transferCoreBlock.contains(QStringLiteral("case FileOperationJob::OVERWRITE_ALL"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must map overwrite-all prompt choices");
    QVERIFY2(
        transferCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::OverwriteAll"), Qt::CaseSensitive),
        "FileTransferJob core-routed section must pass overwrite-all semantics to core");
    QVERIFY2(transferCoreBlock.contains(QStringLiteral("case FileOperationJob::SKIP_ALL"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must map skip-all prompt choices");
    QVERIFY2(transferCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::SkipAll"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must pass skip-all semantics to core");
    QVERIFY2(transferCoreBlock.contains(QStringLiteral("case FileOperationJob::RENAME_ALL"), Qt::CaseSensitive),
             "FileTransferJob core-routed section must map rename-all prompt choices");
    QVERIFY2(
        transferCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::RenameAll"), Qt::CaseSensitive),
        "FileTransferJob core-routed section must pass rename-all semantics to core");
    QVERIFY2(!transferContent.contains(QStringLiteral("G_IO_ERROR_WOULD_RECURSE"), Qt::CaseSensitive),
             "FileTransferJob must not implement adapter-side move-then-copy retry fallback heuristics");

    const auto [deleteContent, deleteError] = readTextFile(QStringLiteral("libfm-qt/src/core/deletejob.cpp"));
    QVERIFY2(deleteError.isEmpty(), qPrintable(deleteError));
    const int deleteExecPos = deleteContent.indexOf(QStringLiteral("void DeleteJob::exec()"));
    QVERIFY2(deleteExecPos >= 0, "deletejob.cpp must define DeleteJob::exec()");
    QVERIFY2(!deleteContent.contains(QStringLiteral("#include \"totalsizejob.h\""), Qt::CaseSensitive),
             "DeleteJob must not include TotalSizeJob pre-scan helpers");
    const int deletePlannerPos = deleteContent.indexOf(QStringLiteral("TotalSizeJob"), deleteExecPos);
    QVERIFY2(deletePlannerPos < 0, "DeleteJob must not run adapter pre-scan planner logic in exec()");

    const int deleteCoreRunnerPos = deleteContent.indexOf(QStringLiteral("auto runCoreRoutedDelete ="), deleteExecPos);
    QVERIFY2(deleteCoreRunnerPos >= 0, "DeleteJob core-routed runner must be present");
    const int deleteCoreIfPos =
        deleteContent.lastIndexOf(QStringLiteral("#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT"), deleteCoreRunnerPos);
    const int deleteCoreElsePos = deleteContent.indexOf(QStringLiteral("#else"), deleteCoreRunnerPos);
    const int deleteCoreEndPos = deleteContent.indexOf(QStringLiteral("#endif"), deleteCoreRunnerPos);
    QVERIFY2(deleteCoreIfPos >= 0 && deleteCoreElsePos > deleteCoreIfPos && deleteCoreEndPos > deleteCoreElsePos,
             "DeleteJob core-routed section must be present");
    const QString deleteCoreBlock = deleteContent.mid(deleteCoreIfPos, deleteCoreElsePos - deleteCoreIfPos);
    const QString deleteLegacyBlock = deleteContent.mid(deleteCoreElsePos, deleteCoreEndPos - deleteCoreElsePos);
    QVERIFY2(!deleteCoreBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "DeleteJob core-routed section must not implement adapter retry loops");
    QVERIFY2(!deleteCoreBlock.contains(QStringLiteral("while (!isCancelled())"), Qt::CaseSensitive),
             "DeleteJob core-routed section must not wrap core execution in retry loops");
    QVERIFY2(deleteLegacyBlock.contains(
                 QStringLiteral("Core file-ops contract unavailable: refusing legacy delete adapter path"),
                 Qt::CaseSensitive),
             "DeleteJob legacy branch must fail fast when the core contract is unavailable");
    QVERIFY2(!deleteLegacyBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "DeleteJob legacy branch must not implement adapter retry loops");
    QVERIFY2(!deleteLegacyBlock.contains(QStringLiteral("while (!isCancelled())"), Qt::CaseSensitive),
             "DeleteJob legacy branch must not wrap legacy delete execution in retry loops");

    const auto [trashContent, trashError] = readTextFile(QStringLiteral("libfm-qt/src/core/trashjob.cpp"));
    QVERIFY2(trashError.isEmpty(), qPrintable(trashError));
    const int trashExecPos = trashContent.indexOf(QStringLiteral("void TrashJob::exec()"));
    QVERIFY2(trashExecPos >= 0, "trashjob.cpp must define TrashJob::exec()");
    const int trashCoreIfPos =
        trashContent.indexOf(QStringLiteral("#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT"), trashExecPos);
    const int trashCoreElsePos = trashContent.indexOf(QStringLiteral("#else"), trashCoreIfPos);
    const int trashCoreEndPos = trashContent.indexOf(QStringLiteral("#endif"), trashCoreElsePos);
    QVERIFY2(trashCoreIfPos >= 0 && trashCoreElsePos > trashCoreIfPos && trashCoreEndPos > trashCoreElsePos,
             "TrashJob core-routed section must be present");
    const QString trashCoreBlock = trashContent.mid(trashCoreIfPos, trashCoreElsePos - trashCoreIfPos);
    const QString trashLegacyBlock = trashContent.mid(trashCoreElsePos, trashCoreEndPos - trashCoreElsePos);
    QVERIFY2(!trashCoreBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "TrashJob core-routed section must not implement adapter retry loops");
    QVERIFY2(!trashCoreBlock.contains(QStringLiteral("g_file_find_enclosing_mount"), Qt::CaseSensitive),
             "TrashJob core-routed section must not probe mounts for behavior decisions");
    QVERIFY2(trashLegacyBlock.contains(
                 QStringLiteral("Core file-ops contract unavailable: refusing legacy trash adapter path"),
                 Qt::CaseSensitive),
             "TrashJob legacy branch must fail fast when the core contract is unavailable");
    QVERIFY2(!trashLegacyBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "TrashJob legacy branch must not implement adapter retry loops");
    QVERIFY2(!trashLegacyBlock.contains(QStringLiteral("for (;;)"), Qt::CaseSensitive),
             "TrashJob legacy branch must not wrap legacy trash execution in retry loops");

    const auto [untrashContent, untrashError] = readTextFile(QStringLiteral("libfm-qt/src/core/untrashjob.cpp"));
    QVERIFY2(untrashError.isEmpty(), qPrintable(untrashError));
    const int untrashExecPos = untrashContent.indexOf(QStringLiteral("void UntrashJob::exec()"));
    QVERIFY2(untrashExecPos >= 0, "untrashjob.cpp must define UntrashJob::exec()");
    const int untrashCoreIfPos =
        untrashContent.indexOf(QStringLiteral("#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT"), untrashExecPos);
    const int untrashCoreElsePos = untrashContent.indexOf(QStringLiteral("#else"), untrashCoreIfPos);
    const int untrashCoreEndPos = untrashContent.indexOf(QStringLiteral("#endif"), untrashCoreElsePos);
    QVERIFY2(untrashCoreIfPos >= 0 && untrashCoreElsePos > untrashCoreIfPos && untrashCoreEndPos > untrashCoreElsePos,
             "UntrashJob core-routed section must be present");
    const QString untrashCoreBlock = untrashContent.mid(untrashCoreIfPos, untrashCoreElsePos - untrashCoreIfPos);
    const QString untrashLegacyBlock = untrashContent.mid(untrashCoreElsePos, untrashCoreEndPos - untrashCoreElsePos);
    QVERIFY2(!untrashCoreBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "UntrashJob core-routed section must not implement adapter retry loops");
    QVERIFY2(!untrashCoreBlock.contains(QStringLiteral("while (!isCancelled())"), Qt::CaseSensitive),
             "UntrashJob core-routed section must not wrap core execution in retry loops");
    QVERIFY2(!untrashCoreBlock.contains(QStringLiteral("queryInfoOrFallback"), Qt::CaseSensitive),
             "UntrashJob core-routed conflict handling must avoid filesystem metadata probing helpers");
    QVERIFY2(!untrashCoreBlock.contains(QStringLiteral("g_file_query_info"), Qt::CaseSensitive),
             "UntrashJob core-routed conflict handling must not query filesystem metadata");
    QVERIFY2(untrashCoreBlock.contains(QStringLiteral("case FileOperationJob::OVERWRITE_ALL"), Qt::CaseSensitive),
             "UntrashJob core-routed section must map overwrite-all prompt choices");
    QVERIFY2(
        untrashCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::OverwriteAll"), Qt::CaseSensitive),
        "UntrashJob core-routed section must pass overwrite-all semantics to core");
    QVERIFY2(untrashCoreBlock.contains(QStringLiteral("case FileOperationJob::SKIP_ALL"), Qt::CaseSensitive),
             "UntrashJob core-routed section must map skip-all prompt choices");
    QVERIFY2(untrashCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::SkipAll"), Qt::CaseSensitive),
             "UntrashJob core-routed section must pass skip-all semantics to core");
    QVERIFY2(untrashCoreBlock.contains(QStringLiteral("case FileOperationJob::RENAME_ALL"), Qt::CaseSensitive),
             "UntrashJob core-routed section must map rename-all prompt choices");
    QVERIFY2(untrashCoreBlock.contains(QStringLiteral("CoreFileOps::ConflictResolution::RenameAll"), Qt::CaseSensitive),
             "UntrashJob core-routed section must pass rename-all semantics to core");
    QVERIFY2(untrashLegacyBlock.contains(
                 QStringLiteral("Core file-ops contract unavailable: refusing legacy untrash adapter path"),
                 Qt::CaseSensitive),
             "UntrashJob legacy branch must fail fast when the core contract is unavailable");
    QVERIFY2(!untrashLegacyBlock.contains(QStringLiteral("ErrorAction::RETRY"), Qt::CaseSensitive),
             "UntrashJob legacy branch must not implement adapter retry loops");
    QVERIFY2(!untrashLegacyBlock.contains(QStringLiteral("while (!isCancelled())"), Qt::CaseSensitive),
             "UntrashJob legacy branch must not wrap legacy untrash execution in retry loops");

    const auto [policyContent, policyError] =
        readTextFile(QStringLiteral("libfm-qt/src/core/fileops_bridge_policy.cpp"));
    QVERIFY2(policyError.isEmpty(), qPrintable(policyError));
    QVERIFY2(!policyContent.contains(QStringLiteral("queryFilesystemRemote"), Qt::CaseSensitive),
             "fileops_bridge_policy.cpp must not probe filesystem metadata for routing behavior");
    QVERIFY2(!policyContent.contains(QStringLiteral("g_file_query_filesystem_info"), Qt::CaseSensitive),
             "fileops_bridge_policy.cpp must not call g_file_query_filesystem_info for routing behavior");
    QVERIFY2(policyContent.contains(QStringLiteral("return RoutingClass::CoreLocal;"), Qt::CaseSensitive),
             "Native local paths must route deterministically to RoutingClass::CoreLocal");
}

QTEST_MAIN(FileOpsInventoryTest)

#include "file_ops_inventory_test.moc"
