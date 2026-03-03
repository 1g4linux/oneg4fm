/*
 * Tests for libfm-qt compatibility bridge to core file-ops engine
 * tests/libfmqt_fileops_bridge_test.cpp
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QVector>

#include "../libfm-qt/src/core/deletejob.h"
#include "../libfm-qt/src/core/fileops_bridge_policy.h"
#include "../libfm-qt/src/core/fileops_request_assembly.h"
#define private public
#include "../libfm-qt/src/core/filetransferjob.h"
#undef private
#include "../libfm-qt/src/fileoperation.h"

#include <cerrno>
#include <cstdint>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

Fm::FilePath toLocalFilePath(const QString& path) {
    const QByteArray encoded = QFile::encodeName(path);
    return Fm::FilePath::fromLocalPath(encoded.constData());
}

QString writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(data);
        file.close();
    }
    return path;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

struct AdapterPromptObservation {
    int promptCount = 0;
    QVector<bool> sourceIsDirectory;
    QVector<bool> destinationIsDirectory;
    QVector<bool> sourceHasTypeAttribute;
    QVector<bool> sourceHasIconAttribute;
    QVector<bool> destinationHasTypeAttribute;
    QVector<bool> destinationHasIconAttribute;
    int errorCount = 0;
    int firstErrorCode = 0;
    int firstErrorDomain = 0;
    QString firstErrorMessage;
    bool cancelled = false;
};

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
namespace CoreFileOps = Oneg4FM::FileOpsContract;

struct CorePromptObservation {
    bool requestBuilt = false;
    bool success = false;
    bool cancelled = false;
    int sysErrno = 0;
    QString buildError;
    int promptCount = 0;
    int conflictCount = 0;
    QVector<bool> destinationIsDirectory;
};

CoreFileOps::ConflictResolution toCoreConflictResolution(Fm::FileOperationJob::FileExistsAction action) {
    switch (action) {
        case Fm::FileOperationJob::OVERWRITE:
            return CoreFileOps::ConflictResolution::Overwrite;
        case Fm::FileOperationJob::SKIP:
            return CoreFileOps::ConflictResolution::Skip;
        case Fm::FileOperationJob::RENAME:
            return CoreFileOps::ConflictResolution::Rename;
        case Fm::FileOperationJob::OVERWRITE_ALL:
            return CoreFileOps::ConflictResolution::OverwriteAll;
        case Fm::FileOperationJob::SKIP_ALL:
            return CoreFileOps::ConflictResolution::SkipAll;
        case Fm::FileOperationJob::RENAME_ALL:
            return CoreFileOps::ConflictResolution::RenameAll;
        case Fm::FileOperationJob::SKIP_ERROR:
            return CoreFileOps::ConflictResolution::Skip;
        case Fm::FileOperationJob::CANCEL:
            return CoreFileOps::ConflictResolution::Abort;
    }
    return CoreFileOps::ConflictResolution::Abort;
}
#endif

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
CorePromptObservation runCorePromptProtocolScenario(const QString& sourcePath,
                                                    const QString& destinationPath,
                                                    Fm::FileOpsRequestAssembly::TransferKind transferKind,
                                                    Fm::FileOperationJob::FileExistsAction responseAction) {
    CorePromptObservation out;

    const Fm::FilePath source = toLocalFilePath(sourcePath);
    const Fm::FilePath destination = toLocalFilePath(destinationPath);
    const auto sourceRouting = Fm::FileOpsBridgePolicy::classifyPathForFileOps(source);
    const auto destinationRouting = Fm::FileOpsBridgePolicy::classifyPathForFileOps(destination);

    if (sourceRouting == Fm::FileOpsBridgePolicy::RoutingClass::Unsupported ||
        destinationRouting == Fm::FileOpsBridgePolicy::RoutingClass::Unsupported) {
        out.buildError = QStringLiteral("Unsupported routing class in prompt protocol scenario");
        return out;
    }

    CoreFileOps::TransferRequest request;
    Fm::GErrorPtr buildError;
    if (!Fm::FileOpsRequestAssembly::buildTransferRequest(
            source, destination, transferKind, sourceRouting, destinationRouting, []() { return false; }, request,
            buildError)) {
        if (buildError) {
            out.buildError = QString::fromUtf8(buildError->message);
        }
        else {
            out.buildError = QStringLiteral("Unable to assemble core transfer request");
        }
        return out;
    }

    out.requestBuilt = true;
    const CoreFileOps::ConflictResolution resolution = toCoreConflictResolution(responseAction);

    CoreFileOps::EventHandlers handlers;
    handlers.onConflict = [&out, resolution](const CoreFileOps::ConflictEvent& event) {
        ++out.conflictCount;
        out.destinationIsDirectory.push_back(event.destinationIsDirectory);
        return resolution;
    };

    CoreFileOps::EventStreamHandlers streamHandlers;
    streamHandlers.onPrompt = [&out](const CoreFileOps::PromptEvent& event) {
        if (event.kind == CoreFileOps::PromptKind::ConflictResolution) {
            ++out.promptCount;
        }
    };

    const CoreFileOps::Result result = CoreFileOps::run(CoreFileOps::toRequest(request), handlers, streamHandlers);
    out.success = result.success;
    out.cancelled = result.cancelled;
    out.sysErrno = result.error.sysErrno;
    return out;
}
#endif

AdapterPromptObservation runAdapterPromptProtocolScenario(const QString& sourcePath,
                                                          const QString& destinationDirectory,
                                                          Fm::FileTransferJob::Mode mode,
                                                          Fm::FileOperationJob::FileExistsAction responseAction) {
    AdapterPromptObservation out;

    Fm::FilePathList sources;
    sources.emplace_back(toLocalFilePath(sourcePath));

    Fm::FileTransferJob job(std::move(sources), mode);
    job.setDestDirPath(toLocalFilePath(destinationDirectory));

    QObject::connect(
        &job, &Fm::FileOperationJob::fileExists, &job,
        [&out, responseAction](const Fm::FileInfo& source, const Fm::FileInfo& destination,
                               Fm::FileOperationJob::FileExistsAction& response, Fm::FilePath&) {
            ++out.promptCount;
            out.sourceIsDirectory.push_back(source.isDir());
            out.destinationIsDirectory.push_back(destination.isDir());

            const auto sourceInfo = source.gFileInfo();
            const auto destinationInfo = destination.gFileInfo();
            out.sourceHasTypeAttribute.push_back(
                sourceInfo.get() && g_file_info_has_attribute(sourceInfo.get(), G_FILE_ATTRIBUTE_STANDARD_TYPE));
            out.sourceHasIconAttribute.push_back(
                sourceInfo.get() && g_file_info_has_attribute(sourceInfo.get(), G_FILE_ATTRIBUTE_STANDARD_ICON));
            out.destinationHasTypeAttribute.push_back(
                destinationInfo.get() &&
                g_file_info_has_attribute(destinationInfo.get(), G_FILE_ATTRIBUTE_STANDARD_TYPE));
            out.destinationHasIconAttribute.push_back(
                destinationInfo.get() &&
                g_file_info_has_attribute(destinationInfo.get(), G_FILE_ATTRIBUTE_STANDARD_ICON));
            response = responseAction;
        },
        Qt::DirectConnection);

    QObject::connect(
        &job, &Fm::Job::error, &job,
        [&out](const Fm::GErrorPtr& err, Fm::Job::ErrorSeverity, Fm::Job::ErrorAction& response) {
            ++out.errorCount;
            if (out.errorCount == 1 && err) {
                out.firstErrorCode = static_cast<int>(err.code());
                out.firstErrorDomain = static_cast<int>(err.domain());
                out.firstErrorMessage = QString::fromUtf8(err->message);
            }
            response = Fm::Job::ErrorAction::CONTINUE;
        },
        Qt::DirectConnection);

    job.run();
    out.cancelled = job.isCancelled();
    return out;
}

bool deviceIdForPath(const QString& path, dev_t& outDevice) {
    struct stat st{};
    const QByteArray native = QFile::encodeName(path);
    if (::stat(native.constData(), &st) != 0) {
        return false;
    }
    outDevice = st.st_dev;
    return true;
}

QString uniqueSuffix() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}  // namespace

class LibfmQtFileOpsBridgeTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void copyConflictSkipCountsAsCompletedWork();
    void copyConflictOverwriteAllPromptsOnceForBatch();
    void deleteNativeDirectoryTreeTracksCompletion();
    void coreRoutingEligibilityAcceptsNativeLocalPath();
    void coreRoutingEligibilityRejectsUriSchemes();
    void bridgePolicyTranslationTablesAreTotalAndStable();
    void noSpaceErrorCleansPartialDestinationWithoutOverwrite();
    void noSpaceErrorPreservesDestinationWithOverwrite();
    void createShortcutFailureIsReportedAsFailure();
    void copyFilesWithExplicitDestPathsAcceptsEmptyLists();
    void nativeCopyMoveDeleteRoutingUsesExplicitClassification();
    void trashAndUntrashJobsRouteViaCoreContract();
    void adapterJobsUseMechanicalOpResultProgressCancelMapping();
    void requestAssemblyIncludesOpIdAndSourceSnapshot();
    void requestAssemblyRejectsPathCountOverLimit();
    void goldenPromptProtocolDestinationConflictMatrix();
    void goldenPromptProtocolNonConflictErrorsDoNotPrompt();
    void goldenPromptProtocolCrossDeviceMoveFallbackPromptsOnce();
};

void LibfmQtFileOpsBridgeTest::copyConflictSkipCountsAsCompletedWork() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString srcFile = writeFile(srcDir + QLatin1String("/note.txt"), QByteArray("source-data"));
    const QString dstFile = writeFile(dstDir + QLatin1String("/note.txt"), QByteArray("existing-data"));

    Fm::FilePathList sources;
    sources.emplace_back(toLocalFilePath(srcFile));

    Fm::FileTransferJob job(std::move(sources), Fm::FileTransferJob::Mode::COPY);
    job.setDestDirPath(toLocalFilePath(dstDir));

    connect(
        &job, &Fm::FileOperationJob::fileExists, &job,
        [](const Fm::FileInfo&, const Fm::FileInfo&, Fm::FileOperationJob::FileExistsAction& response, Fm::FilePath&) {
            response = Fm::FileOperationJob::SKIP;
        },
        Qt::DirectConnection);

    job.run();

    QVERIFY(QFileInfo::exists(srcFile));
    QVERIFY(QFileInfo::exists(dstFile));
    QCOMPARE(readFile(dstFile), QByteArray("existing-data"));

    std::uint64_t totalBytes = 0;
    std::uint64_t totalFiles = 0;
    QVERIFY(job.totalAmount(totalBytes, totalFiles));

    std::uint64_t finishedBytes = 0;
    std::uint64_t finishedFiles = 0;
    QVERIFY(job.finishedAmount(finishedBytes, finishedFiles));

    QCOMPARE(finishedBytes, totalBytes);
    QCOMPARE(finishedFiles, totalFiles);
}

void LibfmQtFileOpsBridgeTest::copyConflictOverwriteAllPromptsOnceForBatch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString sourceTree = srcDir + QLatin1String("/tree");
    QVERIFY(QDir().mkpath(sourceTree));
    const QString srcAlpha = writeFile(sourceTree + QLatin1String("/alpha.txt"), QByteArray("alpha-source"));
    const QString srcBeta = writeFile(sourceTree + QLatin1String("/beta.txt"), QByteArray("beta-source"));

    const QString destTree = dstDir + QLatin1String("/tree");
    QVERIFY(QDir().mkpath(destTree));
    const QString dstAlpha = writeFile(destTree + QLatin1String("/alpha.txt"), QByteArray("alpha-existing"));
    const QString dstBeta = writeFile(destTree + QLatin1String("/beta.txt"), QByteArray("beta-existing"));

    Fm::FilePathList sources;
    sources.emplace_back(toLocalFilePath(sourceTree));

    Fm::FileTransferJob job(std::move(sources), Fm::FileTransferJob::Mode::COPY);
    job.setDestDirPath(toLocalFilePath(dstDir));

    int promptCount = 0;
    connect(
        &job, &Fm::FileOperationJob::fileExists, &job,
        [&promptCount](const Fm::FileInfo&, const Fm::FileInfo&, Fm::FileOperationJob::FileExistsAction& response,
                       Fm::FilePath&) {
            ++promptCount;
            response = Fm::FileOperationJob::OVERWRITE_ALL;
        },
        Qt::DirectConnection);

    job.run();

    QCOMPARE(promptCount, 1);
    QCOMPARE(readFile(dstAlpha), QByteArray("alpha-source"));
    QCOMPARE(readFile(dstBeta), QByteArray("beta-source"));
    QVERIFY(QFileInfo::exists(srcAlpha));
    QVERIFY(QFileInfo::exists(srcBeta));
}

void LibfmQtFileOpsBridgeTest::deleteNativeDirectoryTreeTracksCompletion() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString root = dir.path() + QLatin1String("/tree");
    const QString nested = root + QLatin1String("/nested");
    QVERIFY(QDir().mkpath(nested));

    const QString first = writeFile(root + QLatin1String("/a.txt"), QByteArray("a"));
    const QString second = writeFile(nested + QLatin1String("/b.txt"), QByteArray("b"));
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));

    Fm::FilePathList targets;
    targets.emplace_back(toLocalFilePath(root));

    Fm::DeleteJob job(std::move(targets));
    job.run();

    QVERIFY(!QFileInfo::exists(root));

    std::uint64_t totalBytes = 0;
    std::uint64_t totalFiles = 0;
    QVERIFY(job.totalAmount(totalBytes, totalFiles));

    std::uint64_t finishedBytes = 0;
    std::uint64_t finishedFiles = 0;
    QVERIFY(job.finishedAmount(finishedBytes, finishedFiles));

    QCOMPARE(finishedFiles, totalFiles);
}

void LibfmQtFileOpsBridgeTest::coreRoutingEligibilityAcceptsNativeLocalPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString filePath = writeFile(dir.path() + QLatin1String("/local.txt"), QByteArray("local"));
    const Fm::FilePath path = toLocalFilePath(filePath);
    QVERIFY(path.isNative());
    QVERIFY(Fm::FileOpsBridgePolicy::isCoreLocalPathEligible(path));
}

void LibfmQtFileOpsBridgeTest::coreRoutingEligibilityRejectsUriSchemes() {
    const Fm::FilePath trashRoot = Fm::FilePath::fromUri("trash:///");
    QVERIFY(!trashRoot.isNative());
    QVERIFY(!Fm::FileOpsBridgePolicy::isCoreLocalPathEligible(trashRoot));

    const Fm::FilePath remoteUri = Fm::FilePath::fromUri("sftp://example.invalid/path");
    QVERIFY(!remoteUri.isNative());
    QVERIFY(!Fm::FileOpsBridgePolicy::isCoreLocalPathEligible(remoteUri));
}

void LibfmQtFileOpsBridgeTest::bridgePolicyTranslationTablesAreTotalAndStable() {
    using RoutingClass = Fm::FileOpsBridgePolicy::RoutingClass;
    QCOMPARE(QString::fromLatin1(Fm::FileOpsBridgePolicy::routingClassName(RoutingClass::CoreLocal)),
             QStringLiteral("CoreLocal"));
    QCOMPARE(QString::fromLatin1(Fm::FileOpsBridgePolicy::routingClassName(RoutingClass::LegacyGio)),
             QStringLiteral("LegacyGio"));
    QCOMPARE(QString::fromLatin1(Fm::FileOpsBridgePolicy::routingClassName(RoutingClass::Unsupported)),
             QStringLiteral("Unsupported"));

#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    namespace CoreFileOps = Oneg4FM::FileOpsContract;

    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::CoreLocal), CoreFileOps::Backend::LocalHardened);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::LegacyGio), CoreFileOps::Backend::Gio);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::Unsupported), CoreFileOps::Backend::LocalHardened);

    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::CoreLocal, RoutingClass::CoreLocal),
             CoreFileOps::Backend::LocalHardened);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::CoreLocal, RoutingClass::LegacyGio),
             CoreFileOps::Backend::Gio);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::CoreLocal, RoutingClass::Unsupported),
             CoreFileOps::Backend::LocalHardened);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::LegacyGio, RoutingClass::CoreLocal),
             CoreFileOps::Backend::Gio);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::LegacyGio, RoutingClass::LegacyGio),
             CoreFileOps::Backend::Gio);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::LegacyGio, RoutingClass::Unsupported),
             CoreFileOps::Backend::Gio);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::Unsupported, RoutingClass::CoreLocal),
             CoreFileOps::Backend::LocalHardened);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::Unsupported, RoutingClass::LegacyGio),
             CoreFileOps::Backend::Gio);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreBackend(RoutingClass::Unsupported, RoutingClass::Unsupported),
             CoreFileOps::Backend::LocalHardened);

    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreTransferOperation(Fm::FileOpsBridgePolicy::TransferKind::Copy),
             CoreFileOps::TransferOperation::Copy);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreTransferOperation(Fm::FileOpsBridgePolicy::TransferKind::Move),
             CoreFileOps::TransferOperation::Move);

    bool mapped = false;
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::CANCEL, &mapped),
             CoreFileOps::ConflictResolution::Abort);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::OVERWRITE, &mapped),
             CoreFileOps::ConflictResolution::Overwrite);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::OVERWRITE_ALL, &mapped),
             CoreFileOps::ConflictResolution::OverwriteAll);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::RENAME, &mapped),
             CoreFileOps::ConflictResolution::Rename);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::RENAME_ALL, &mapped),
             CoreFileOps::ConflictResolution::RenameAll);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::SKIP, &mapped),
             CoreFileOps::ConflictResolution::Skip);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::SKIP_ERROR, &mapped),
             CoreFileOps::ConflictResolution::Skip);
    QVERIFY(mapped);
    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(Fm::FileOperationJob::SKIP_ALL, &mapped),
             CoreFileOps::ConflictResolution::SkipAll);
    QVERIFY(mapped);

    QCOMPARE(Fm::FileOpsBridgePolicy::toCoreConflictResolution(
                 static_cast<Fm::FileOperationJob::FileExistsAction>(0x7fffffff), &mapped),
             CoreFileOps::ConflictResolution::Abort);
    QVERIFY(!mapped);
#else
    const QString policyPath = QFINDTESTDATA("../libfm-qt/src/core/fileops_bridge_policy.cpp");
    QVERIFY2(!policyPath.isEmpty(), "Unable to locate libfm-qt/src/core/fileops_bridge_policy.cpp");
    QFile policySource(policyPath);
    QVERIFY(policySource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray policyBytes = policySource.readAll();
    QVERIFY(policyBytes.contains("kRoutingBackendMap"));
    QVERIFY(policyBytes.contains("kRoutingPairBackendMap"));
    QVERIFY(policyBytes.contains("kTransferOperationMap"));
    QVERIFY(policyBytes.contains("kConflictResolutionMap"));
    QVERIFY(policyBytes.contains("FileOperationJob::CANCEL"));
    QVERIFY(policyBytes.contains("FileOperationJob::OVERWRITE"));
    QVERIFY(policyBytes.contains("FileOperationJob::OVERWRITE_ALL"));
    QVERIFY(policyBytes.contains("FileOperationJob::RENAME"));
    QVERIFY(policyBytes.contains("FileOperationJob::RENAME_ALL"));
    QVERIFY(policyBytes.contains("FileOperationJob::SKIP"));
    QVERIFY(policyBytes.contains("FileOperationJob::SKIP_ERROR"));
    QVERIFY(policyBytes.contains("FileOperationJob::SKIP_ALL"));
#endif
}

void LibfmQtFileOpsBridgeTest::noSpaceErrorCleansPartialDestinationWithoutOverwrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcFile = writeFile(dir.path() + QLatin1String("/source.txt"), QByteArray("source-data"));
    const QString dstFile = writeFile(dir.path() + QLatin1String("/partial.txt"), QByteArray("partial-data"));
    QVERIFY(QFileInfo::exists(dstFile));

    Fm::FileTransferJob job(Fm::FilePathList{}, Fm::FileTransferJob::Mode::COPY);
    Fm::FilePath srcPath = toLocalFilePath(srcFile);
    Fm::FilePath destPath = toLocalFilePath(dstFile);
    Fm::GFileInfoPtr srcInfo{g_file_info_new(), false};
    g_file_info_set_display_name(srcInfo.get(), "source.txt");

    Fm::GErrorPtr err{G_IO_ERROR, G_IO_ERROR_NO_SPACE, "No space left on device"};
    int flags = 0;

    const bool retry = job.handleError(err, srcPath, srcInfo, destPath, flags);
    QVERIFY(!retry);
    QVERIFY(!err);
    QVERIFY(!QFileInfo::exists(dstFile));
}

void LibfmQtFileOpsBridgeTest::noSpaceErrorPreservesDestinationWithOverwrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcFile = writeFile(dir.path() + QLatin1String("/source.txt"), QByteArray("source-data"));
    const QString dstFile = writeFile(dir.path() + QLatin1String("/existing.txt"), QByteArray("existing-data"));
    QVERIFY(QFileInfo::exists(dstFile));

    Fm::FileTransferJob job(Fm::FilePathList{}, Fm::FileTransferJob::Mode::COPY);
    Fm::FilePath srcPath = toLocalFilePath(srcFile);
    Fm::FilePath destPath = toLocalFilePath(dstFile);
    Fm::GFileInfoPtr srcInfo{g_file_info_new(), false};
    g_file_info_set_display_name(srcInfo.get(), "source.txt");

    Fm::GErrorPtr err{G_IO_ERROR, G_IO_ERROR_NO_SPACE, "No space left on device"};
    int flags = G_FILE_COPY_OVERWRITE;

    const bool retry = job.handleError(err, srcPath, srcInfo, destPath, flags);
    QVERIFY(!retry);
    QVERIFY(!err);
    QVERIFY(QFileInfo::exists(dstFile));
    QCOMPARE(readFile(dstFile), QByteArray("existing-data"));
}

void LibfmQtFileOpsBridgeTest::createShortcutFailureIsReportedAsFailure() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString missingParent = dir.path() + QLatin1String("/missing");
    const QString shortcutPath = missingParent + QLatin1String("/remote.desktop");
    QVERIFY(!QFileInfo::exists(missingParent));

    Fm::FileTransferJob job(Fm::FilePathList{}, Fm::FileTransferJob::Mode::LINK);
    Fm::FilePath srcPath = Fm::FilePath::fromUri("sftp://example.invalid/path/to/file");
    Fm::FilePath destPath = toLocalFilePath(shortcutPath);
    Fm::GFileInfoPtr srcInfo{g_file_info_new(), false};
    g_file_info_set_display_name(srcInfo.get(), "Remote File");
    GIcon* icon = g_themed_icon_new("text-x-generic");
    g_file_info_set_icon(srcInfo.get(), icon);
    g_object_unref(icon);

    QVERIFY(!job.createShortcut(srcPath, srcInfo, destPath));
    QVERIFY(!QFileInfo::exists(shortcutPath));
}

void LibfmQtFileOpsBridgeTest::copyFilesWithExplicitDestPathsAcceptsEmptyLists() {
    Fm::FilePathList srcFiles;
    Fm::FilePathList destFiles;
    Fm::FileOperation::copyFiles(std::move(srcFiles), std::move(destFiles), nullptr);
    QTest::qWait(10);
    QVERIFY(true);
}

void LibfmQtFileOpsBridgeTest::nativeCopyMoveDeleteRoutingUsesExplicitClassification() {
    const QString transferPath = QFINDTESTDATA("../libfm-qt/src/core/filetransferjob.cpp");
    QVERIFY2(!transferPath.isEmpty(), "Unable to locate libfm-qt/src/core/filetransferjob.cpp");
    QFile transferSource(transferPath);
    QVERIFY(transferSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray transferBytes = transferSource.readAll();

    QVERIFY(transferBytes.contains("classifyPathForFileOps(srcPath)"));
    QVERIFY(transferBytes.contains("classifyPathForFileOps(destPath)"));
    QVERIFY(transferBytes.contains("RoutingClass::Unsupported"));
    QVERIFY(transferBytes.contains("CoreFileOps::TransferRequest"));
    QVERIFY(transferBytes.contains("FileOpsRequestAssembly::buildTransferRequest"));
    QVERIFY(transferBytes.contains("FileOpsRequestAssembly::validateRequestPathCount"));
    QVERIFY(transferBytes.contains("CoreFileOps::EventStreamHandlers streamHandlers"));
    QVERIFY(transferBytes.contains("streamHandlers.onPrompt"));
    QVERIFY(transferBytes.contains("streamHandlers.onConflict"));
    QVERIFY(transferBytes.contains("FileOpsBridgePolicy::toCoreConflictResolution"));
    QVERIFY(transferBytes.contains("CoreFileOps::run(CoreFileOps::toRequest(request), handlers, streamHandlers)"));
    QVERIFY(transferBytes.contains("runCoreRoutedPath(srcPath, destPath, srcRouting, destRouting)"));

    const QString deletePath = QFINDTESTDATA("../libfm-qt/src/core/deletejob.cpp");
    QVERIFY2(!deletePath.isEmpty(), "Unable to locate libfm-qt/src/core/deletejob.cpp");
    QFile deleteSource(deletePath);
    QVERIFY(deleteSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray deleteBytes = deleteSource.readAll();

    QVERIFY(deleteBytes.contains("classifyPathForFileOps(path)"));
    QVERIFY(deleteBytes.contains("RoutingClass::Unsupported"));
    QVERIFY(deleteBytes.contains("CoreFileOps::DeleteRequest"));
    QVERIFY(deleteBytes.contains("FileOpsRequestAssembly::buildDeleteRequest"));
    QVERIFY(deleteBytes.contains("FileOpsRequestAssembly::validateRequestPathCount"));
    QVERIFY(deleteBytes.contains("runCoreRoutedDelete(path, pathRouting)"));

    const QString trashPath = QFINDTESTDATA("../libfm-qt/src/core/trashjob.cpp");
    QVERIFY2(!trashPath.isEmpty(), "Unable to locate libfm-qt/src/core/trashjob.cpp");
    QFile trashSource(trashPath);
    QVERIFY(trashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray trashBytes = trashSource.readAll();
    QVERIFY(trashBytes.contains("CoreFileOps::TrashRequest"));

    const QString untrashPath = QFINDTESTDATA("../libfm-qt/src/core/untrashjob.cpp");
    QVERIFY2(!untrashPath.isEmpty(), "Unable to locate libfm-qt/src/core/untrashjob.cpp");
    QFile untrashSource(untrashPath);
    QVERIFY(untrashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray untrashBytes = untrashSource.readAll();
    QVERIFY(untrashBytes.contains("CoreFileOps::UntrashRequest"));

    const QString policyPath = QFINDTESTDATA("../libfm-qt/src/core/fileops_bridge_policy.cpp");
    QVERIFY2(!policyPath.isEmpty(), "Unable to locate libfm-qt/src/core/fileops_bridge_policy.cpp");
    QFile policySource(policyPath);
    QVERIFY(policySource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray policyBytes = policySource.readAll();
    QVERIFY(policyBytes.contains("RoutingClass::CoreLocal"));
    QVERIFY(policyBytes.contains("RoutingClass::LegacyGio"));
    QVERIFY(policyBytes.contains("RoutingClass::Unsupported"));
}

void LibfmQtFileOpsBridgeTest::trashAndUntrashJobsRouteViaCoreContract() {
    const QString trashPath = QFINDTESTDATA("../libfm-qt/src/core/trashjob.cpp");
    QVERIFY2(!trashPath.isEmpty(), "Unable to locate libfm-qt/src/core/trashjob.cpp");
    QFile trashSource(trashPath);
    QVERIFY(trashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray trashBytes = trashSource.readAll();
    QVERIFY(trashBytes.contains("CoreFileOps::TrashRequest request"));
    QVERIFY(trashBytes.contains("FileOpsRequestAssembly::buildTrashRequest"));
    QVERIFY(trashBytes.contains("FileOpsRequestAssembly::validateRequestPathCount"));
    QVERIFY(trashBytes.contains("CoreFileOps::runOp(request, handlers)"));

    const QString untrashPath = QFINDTESTDATA("../libfm-qt/src/core/untrashjob.cpp");
    QVERIFY2(!untrashPath.isEmpty(), "Unable to locate libfm-qt/src/core/untrashjob.cpp");
    QFile untrashSource(untrashPath);
    QVERIFY(untrashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray untrashBytes = untrashSource.readAll();
    QVERIFY(untrashBytes.contains("CoreFileOps::UntrashRequest request"));
    QVERIFY(untrashBytes.contains("FileOpsRequestAssembly::buildUntrashRequest"));
    QVERIFY(untrashBytes.contains("FileOpsRequestAssembly::validateRequestPathCount"));
    QVERIFY(untrashBytes.contains("CoreFileOps::EventStreamHandlers streamHandlers"));
    QVERIFY(untrashBytes.contains("streamHandlers.onPrompt"));
    QVERIFY(untrashBytes.contains("streamHandlers.onConflict"));
    QVERIFY(untrashBytes.contains("FileOpsBridgePolicy::toCoreConflictResolution"));
    QVERIFY(untrashBytes.contains("CoreFileOps::run(CoreFileOps::toRequest(request), handlers, streamHandlers)"));
}

void LibfmQtFileOpsBridgeTest::adapterJobsUseMechanicalOpResultProgressCancelMapping() {
    const QString transferPath = QFINDTESTDATA("../libfm-qt/src/core/filetransferjob.cpp");
    QVERIFY2(!transferPath.isEmpty(), "Unable to locate libfm-qt/src/core/filetransferjob.cpp");
    QFile transferSource(transferPath);
    QVERIFY(transferSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray transferBytes = transferSource.readAll();
    QVERIFY(transferBytes.contains("CoreFileOps::OpResult"));
    QVERIFY(transferBytes.contains("CoreFileOps::toOpResult("));
    QVERIFY(transferBytes.contains("CoreFileOps::OpStatus::Cancelled"));

    const QString deletePath = QFINDTESTDATA("../libfm-qt/src/core/deletejob.cpp");
    QVERIFY2(!deletePath.isEmpty(), "Unable to locate libfm-qt/src/core/deletejob.cpp");
    QFile deleteSource(deletePath);
    QVERIFY(deleteSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray deleteBytes = deleteSource.readAll();
    QVERIFY(deleteBytes.contains("CoreFileOps::OpResult"));
    QVERIFY(deleteBytes.contains("CoreFileOps::runOp("));
    QVERIFY(deleteBytes.contains("CoreFileOps::OpStatus::Cancelled"));

    const QString trashPath = QFINDTESTDATA("../libfm-qt/src/core/trashjob.cpp");
    QVERIFY2(!trashPath.isEmpty(), "Unable to locate libfm-qt/src/core/trashjob.cpp");
    QFile trashSource(trashPath);
    QVERIFY(trashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray trashBytes = trashSource.readAll();
    QVERIFY(trashBytes.contains("CoreFileOps::EventHandlers handlers"));
    QVERIFY(trashBytes.contains("handlers.onProgress"));
    QVERIFY(trashBytes.contains("CoreFileOps::OpResult"));
    QVERIFY(trashBytes.contains("CoreFileOps::runOp("));
    QVERIFY(trashBytes.contains("CoreFileOps::OpStatus::Cancelled"));

    const QString untrashPath = QFINDTESTDATA("../libfm-qt/src/core/untrashjob.cpp");
    QVERIFY2(!untrashPath.isEmpty(), "Unable to locate libfm-qt/src/core/untrashjob.cpp");
    QFile untrashSource(untrashPath);
    QVERIFY(untrashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray untrashBytes = untrashSource.readAll();
    QVERIFY(untrashBytes.contains("CoreFileOps::OpResult"));
    QVERIFY(untrashBytes.contains("CoreFileOps::toOpResult("));
    QVERIFY(untrashBytes.contains("CoreFileOps::OpStatus::Cancelled"));

    const QString assemblyPath = QFINDTESTDATA("../libfm-qt/src/core/fileops_request_assembly.cpp");
    QVERIFY2(!assemblyPath.isEmpty(), "Unable to locate libfm-qt/src/core/fileops_request_assembly.cpp");
    QFile assemblySource(assemblyPath);
    QVERIFY(assemblySource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray assemblyBytes = assemblySource.readAll();
    QVERIFY(assemblyBytes.contains("cancelHandle.cancel();"));
    QVERIFY(assemblyBytes.contains("return cancelHandle.isCancelled();"));
}

void LibfmQtFileOpsBridgeTest::requestAssemblyIncludesOpIdAndSourceSnapshot() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = writeFile(dir.path() + QLatin1String("/snapshot-src.txt"), QByteArray("snapshot-data"));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/snapshot-src.txt");

    const Fm::FilePath source = toLocalFilePath(srcPath);
    const Fm::FilePath destination = toLocalFilePath(dstPath);
    const auto srcRouting = Fm::FileOpsBridgePolicy::classifyPathForFileOps(source);
    const auto dstRouting = Fm::FileOpsBridgePolicy::classifyPathForFileOps(destination);
    QCOMPARE(srcRouting, Fm::FileOpsBridgePolicy::RoutingClass::CoreLocal);
    QCOMPARE(dstRouting, Fm::FileOpsBridgePolicy::RoutingClass::CoreLocal);

    Oneg4FM::FileOpsContract::TransferRequest request;
    Fm::GErrorPtr err;
    QVERIFY(Fm::FileOpsRequestAssembly::buildTransferRequest(
        source, destination, Fm::FileOpsRequestAssembly::TransferKind::Copy, srcRouting, dstRouting,
        []() { return false; }, request, err));
    QVERIFY(!err);

    QVERIFY(!request.common.opId.empty());
    QCOMPARE(request.common.sources.size(), std::size_t(1));
    QCOMPARE(request.common.sourceSnapshots.size(), std::size_t(1));
    QVERIFY(request.common.sourceSnapshots[0].available);
    QVERIFY(!request.common.uiContext.initiator.empty());
    QVERIFY(request.common.uiContext.initiator.find("snapshot=") != std::string::npos);
#else
    QSKIP("Core file-ops contract disabled");
#endif
}

void LibfmQtFileOpsBridgeTest::requestAssemblyRejectsPathCountOverLimit() {
    Fm::GErrorPtr err;
    QVERIFY(!Fm::FileOpsRequestAssembly::validateRequestPathCount(Fm::FileOpsRequestAssembly::kMaxPathsPerRequest + 1,
                                                                  "copy/move", err));
    QVERIFY(err);
    QCOMPARE(static_cast<int>(err.domain()), static_cast<int>(G_IO_ERROR));
    QCOMPARE(static_cast<int>(err.code()), static_cast<int>(G_IO_ERROR_INVALID_ARGUMENT));
}

void LibfmQtFileOpsBridgeTest::goldenPromptProtocolDestinationConflictMatrix() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString srcFile = writeFile(srcDir + QLatin1String("/item.txt"), QByteArray("source-file"));
    const QString dstFile = writeFile(dstDir + QLatin1String("/item.txt"), QByteArray("existing-file"));

    const CorePromptObservation coreFileFile = runCorePromptProtocolScenario(
        srcFile, dstFile, Fm::FileOpsRequestAssembly::TransferKind::Copy, Fm::FileOperationJob::SKIP);
    QVERIFY2(coreFileFile.requestBuilt, qPrintable(coreFileFile.buildError));
    QCOMPARE(coreFileFile.promptCount, 1);
    QCOMPARE(coreFileFile.conflictCount, 1);
    QCOMPARE(coreFileFile.destinationIsDirectory.size(), 1);
    QVERIFY(!coreFileFile.destinationIsDirectory.constFirst());
    QVERIFY(coreFileFile.success);

    const AdapterPromptObservation adapterFileFile =
        runAdapterPromptProtocolScenario(srcFile, dstDir, Fm::FileTransferJob::Mode::COPY, Fm::FileOperationJob::SKIP);
    QCOMPARE(adapterFileFile.promptCount, coreFileFile.promptCount);
    QCOMPARE(adapterFileFile.sourceIsDirectory.size(), 1);
    QVERIFY(!adapterFileFile.sourceIsDirectory.constFirst());
    QCOMPARE(adapterFileFile.destinationIsDirectory.size(), 1);
    QVERIFY(!adapterFileFile.destinationIsDirectory.constFirst());
    QCOMPARE(adapterFileFile.sourceHasTypeAttribute.size(), 1);
    QVERIFY(adapterFileFile.sourceHasTypeAttribute.constFirst());
    QCOMPARE(adapterFileFile.sourceHasIconAttribute.size(), 1);
    QVERIFY(adapterFileFile.sourceHasIconAttribute.constFirst());
    QCOMPARE(adapterFileFile.destinationHasTypeAttribute.size(), 1);
    QVERIFY(adapterFileFile.destinationHasTypeAttribute.constFirst());
    QCOMPARE(adapterFileFile.destinationHasIconAttribute.size(), 1);
    QVERIFY(adapterFileFile.destinationHasIconAttribute.constFirst());
    QCOMPARE(adapterFileFile.errorCount, 0);
    QVERIFY(!adapterFileFile.cancelled);
    QCOMPARE(readFile(dstFile), QByteArray("existing-file"));

    const QString srcTree = srcDir + QLatin1String("/tree");
    const QString dstTree = dstDir + QLatin1String("/tree");
    QVERIFY(QDir().mkpath(srcTree));
    QVERIFY(QDir().mkpath(dstTree));
    const QString srcNested = writeFile(srcTree + QLatin1String("/nested.txt"), QByteArray("tree-source"));
    const QString dstNested = writeFile(dstTree + QLatin1String("/nested.txt"), QByteArray("tree-existing"));

    const CorePromptObservation coreDirDir = runCorePromptProtocolScenario(
        srcTree, dstTree, Fm::FileOpsRequestAssembly::TransferKind::Copy, Fm::FileOperationJob::SKIP);
    QVERIFY2(coreDirDir.requestBuilt, qPrintable(coreDirDir.buildError));
    QCOMPARE(coreDirDir.promptCount, 1);
    QCOMPARE(coreDirDir.conflictCount, 1);
    QCOMPARE(coreDirDir.destinationIsDirectory.size(), 1);
    QVERIFY(coreDirDir.destinationIsDirectory.constFirst());
    QVERIFY(coreDirDir.success);

    const AdapterPromptObservation adapterDirDir =
        runAdapterPromptProtocolScenario(srcTree, dstDir, Fm::FileTransferJob::Mode::COPY, Fm::FileOperationJob::SKIP);
    QCOMPARE(adapterDirDir.promptCount, coreDirDir.promptCount);
    QCOMPARE(adapterDirDir.sourceIsDirectory.size(), 1);
    QVERIFY(adapterDirDir.sourceIsDirectory.constFirst());
    QCOMPARE(adapterDirDir.destinationIsDirectory.size(), 1);
    QVERIFY(adapterDirDir.destinationIsDirectory.constFirst());
    QCOMPARE(adapterDirDir.errorCount, 0);
    QVERIFY(!adapterDirDir.cancelled);
    QCOMPARE(readFile(dstNested), QByteArray("tree-existing"));
    QVERIFY(QFileInfo::exists(srcNested));

    const QString srcFileVsDir = writeFile(srcDir + QLatin1String("/namedir.txt"), QByteArray("file-source"));
    const QString dstFileVsDir = dstDir + QLatin1String("/namedir.txt");
    QVERIFY(QDir().mkpath(dstFileVsDir));

    const CorePromptObservation coreFileDir = runCorePromptProtocolScenario(
        srcFileVsDir, dstFileVsDir, Fm::FileOpsRequestAssembly::TransferKind::Copy, Fm::FileOperationJob::SKIP);
    QVERIFY2(coreFileDir.requestBuilt, qPrintable(coreFileDir.buildError));
    QCOMPARE(coreFileDir.promptCount, 1);
    QCOMPARE(coreFileDir.conflictCount, 1);
    QCOMPARE(coreFileDir.destinationIsDirectory.size(), 1);
    QVERIFY(coreFileDir.destinationIsDirectory.constFirst());
    QVERIFY(coreFileDir.success);

    const AdapterPromptObservation adapterFileDir = runAdapterPromptProtocolScenario(
        srcFileVsDir, dstDir, Fm::FileTransferJob::Mode::COPY, Fm::FileOperationJob::SKIP);
    QCOMPARE(adapterFileDir.promptCount, coreFileDir.promptCount);
    QCOMPARE(adapterFileDir.sourceIsDirectory.size(), 1);
    QVERIFY(!adapterFileDir.sourceIsDirectory.constFirst());
    QCOMPARE(adapterFileDir.destinationIsDirectory.size(), 1);
    QVERIFY(adapterFileDir.destinationIsDirectory.constFirst());
    QCOMPARE(adapterFileDir.errorCount, 0);
    QVERIFY(!adapterFileDir.cancelled);
    QVERIFY(QFileInfo::exists(dstFileVsDir));
    QVERIFY(QFileInfo(dstFileVsDir).isDir());
#else
    QSKIP("Core file-ops contract disabled");
#endif
}

void LibfmQtFileOpsBridgeTest::goldenPromptProtocolNonConflictErrorsDoNotPrompt() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    if (::geteuid() != 0) {
        const QString srcPath = writeFile(dir.path() + QLatin1String("/deny.txt"), QByteArray("deny-data"));
        const QString deniedDir = dir.path() + QLatin1String("/deny-dst");
        QVERIFY(QDir().mkpath(deniedDir));
        QVERIFY(::chmod(QFile::encodeName(deniedDir).constData(), 0555) == 0);
        const QString deniedDestination = deniedDir + QLatin1String("/deny.txt");

        const CorePromptObservation coreDenied =
            runCorePromptProtocolScenario(srcPath, deniedDestination, Fm::FileOpsRequestAssembly::TransferKind::Copy,
                                          Fm::FileOperationJob::OVERWRITE);
        QVERIFY2(coreDenied.requestBuilt, qPrintable(coreDenied.buildError));
        QVERIFY(!coreDenied.success);
        QCOMPARE(coreDenied.promptCount, 0);
        QCOMPARE(coreDenied.conflictCount, 0);
        QVERIFY(coreDenied.sysErrno == EACCES || coreDenied.sysErrno == EPERM);

        const AdapterPromptObservation adapterDenied = runAdapterPromptProtocolScenario(
            srcPath, deniedDir, Fm::FileTransferJob::Mode::COPY, Fm::FileOperationJob::OVERWRITE);
        QCOMPARE(adapterDenied.promptCount, coreDenied.promptCount);
        QVERIFY(adapterDenied.errorCount >= 1);
        QCOMPARE(adapterDenied.firstErrorCode, g_io_error_from_errno(coreDenied.sysErrno));
        QCOMPARE(adapterDenied.firstErrorDomain, static_cast<int>(G_IO_ERROR));
        QVERIFY(!QFileInfo::exists(deniedDestination));

        QVERIFY(::chmod(QFile::encodeName(deniedDir).constData(), 0755) == 0);
    }

    const QString longSrc = writeFile(dir.path() + QLatin1String("/long.txt"), QByteArray("long-data"));
    const QString longName(static_cast<int>(NAME_MAX + 8), QLatin1Char('n'));
    const QString longDir = dir.path() + QLatin1Char('/') + longName;
    const QString longDestination = longDir + QLatin1String("/long.txt");

    const CorePromptObservation coreLong = runCorePromptProtocolScenario(
        longSrc, longDestination, Fm::FileOpsRequestAssembly::TransferKind::Copy, Fm::FileOperationJob::OVERWRITE);
    QVERIFY2(coreLong.requestBuilt, qPrintable(coreLong.buildError));
    QVERIFY(!coreLong.success);
    QCOMPARE(coreLong.promptCount, 0);
    QCOMPARE(coreLong.conflictCount, 0);
    QCOMPARE(coreLong.sysErrno, ENAMETOOLONG);

    const AdapterPromptObservation adapterLong = runAdapterPromptProtocolScenario(
        longSrc, longDir, Fm::FileTransferJob::Mode::COPY, Fm::FileOperationJob::OVERWRITE);
    QCOMPARE(adapterLong.promptCount, coreLong.promptCount);
    QVERIFY(adapterLong.errorCount >= 1);
    QCOMPARE(adapterLong.firstErrorCode, g_io_error_from_errno(coreLong.sysErrno));
    QCOMPARE(adapterLong.firstErrorDomain, static_cast<int>(G_IO_ERROR));
    QVERIFY(!QFileInfo::exists(longDestination));
#else
    QSKIP("Core file-ops contract disabled");
#endif
}

void LibfmQtFileOpsBridgeTest::goldenPromptProtocolCrossDeviceMoveFallbackPromptsOnce() {
#if LIBFM_QT_HAS_CORE_FILEOPS_CONTRACT
    QTemporaryDir srcRoot;
    QVERIFY(srcRoot.isValid());

    const QString destinationRoot = QStringLiteral("/dev/shm/oneg4fm-prompt-%1").arg(uniqueSuffix());
    if (!QDir().mkpath(destinationRoot)) {
        QSKIP("Skipping cross-device prompt protocol test: unable to create destination under /dev/shm");
    }

    dev_t srcDevice = 0;
    dev_t destinationDevice = 0;
    if (!deviceIdForPath(srcRoot.path(), srcDevice) || !deviceIdForPath(destinationRoot, destinationDevice)) {
        QDir(destinationRoot).removeRecursively();
        QSKIP("Skipping cross-device prompt protocol test: unable to read device ids");
    }
    if (srcDevice == destinationDevice) {
        QDir(destinationRoot).removeRecursively();
        QSKIP("Skipping cross-device prompt protocol test: source and destination share the same device id");
    }

    const QString coreSourcePath =
        writeFile(srcRoot.path() + QLatin1String("/core-move.txt"), QByteArray("core-source"));
    const QString coreDestinationPath =
        writeFile(destinationRoot + QLatin1String("/core-move.txt"), QByteArray("core-existing"));

    const QString adapterSourcePath =
        writeFile(srcRoot.path() + QLatin1String("/adapter-move.txt"), QByteArray("adapter-source"));
    const QString adapterDestinationPath =
        writeFile(destinationRoot + QLatin1String("/adapter-move.txt"), QByteArray("adapter-existing"));

    const CorePromptObservation coreMove =
        runCorePromptProtocolScenario(coreSourcePath, coreDestinationPath,
                                      Fm::FileOpsRequestAssembly::TransferKind::Move, Fm::FileOperationJob::OVERWRITE);
    QVERIFY2(coreMove.requestBuilt, qPrintable(coreMove.buildError));
    QVERIFY(coreMove.success);
    QCOMPARE(coreMove.promptCount, 1);
    QCOMPARE(coreMove.conflictCount, 1);
    QCOMPARE(coreMove.destinationIsDirectory.size(), 1);
    QVERIFY(!coreMove.destinationIsDirectory.constFirst());

    const AdapterPromptObservation adapterMove = runAdapterPromptProtocolScenario(
        adapterSourcePath, destinationRoot, Fm::FileTransferJob::Mode::MOVE, Fm::FileOperationJob::OVERWRITE);
    QCOMPARE(adapterMove.promptCount, coreMove.promptCount);
    QCOMPARE(adapterMove.errorCount, 0);
    QVERIFY(!adapterMove.cancelled);
    QVERIFY(!QFileInfo::exists(adapterSourcePath));
    QCOMPARE(readFile(adapterDestinationPath), QByteArray("adapter-source"));

    QVERIFY(QDir(destinationRoot).removeRecursively());
#else
    QSKIP("Core file-ops contract disabled");
#endif
}

QTEST_MAIN(LibfmQtFileOpsBridgeTest)

#include "libfmqt_fileops_bridge_test.moc"
