/*
 * Tests for libfm-qt compatibility bridge to core file-ops engine
 * tests/libfmqt_fileops_bridge_test.cpp
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "../libfm-qt/src/core/deletejob.h"
#include "../libfm-qt/src/core/fileops_bridge_policy.h"
#include "../libfm-qt/src/core/fileops_request_assembly.h"
#define private public
#include "../libfm-qt/src/core/filetransferjob.h"
#undef private
#include "../libfm-qt/src/fileoperation.h"

#include <cstdint>
#include <string>

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

}  // namespace

class LibfmQtFileOpsBridgeTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void copyConflictSkipCountsAsCompletedWork();
    void copyConflictOverwriteAllPromptsOnceForBatch();
    void deleteNativeDirectoryTreeTracksCompletion();
    void coreRoutingEligibilityAcceptsNativeLocalPath();
    void coreRoutingEligibilityRejectsUriSchemes();
    void noSpaceErrorCleansPartialDestinationWithoutOverwrite();
    void noSpaceErrorPreservesDestinationWithOverwrite();
    void createShortcutFailureIsReportedAsFailure();
    void copyFilesWithExplicitDestPathsAcceptsEmptyLists();
    void nativeCopyMoveDeleteRoutingUsesExplicitClassification();
    void trashAndUntrashJobsRouteViaCoreContract();
    void requestAssemblyIncludesOpIdAndSourceSnapshot();
    void requestAssemblyRejectsPathCountOverLimit();
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
    QVERIFY(transferBytes.contains("RoutingClass::CoreLocal"));
    QVERIFY(transferBytes.contains("RoutingClass::LegacyGio"));
    QVERIFY(transferBytes.contains("RoutingClass::Unsupported"));
    QVERIFY(transferBytes.contains("CoreFileOps::TransferRequest"));
    QVERIFY(transferBytes.contains("FileOpsRequestAssembly::buildTransferRequest"));
    QVERIFY(transferBytes.contains("FileOpsRequestAssembly::validateRequestPathCount"));
    QVERIFY(transferBytes.contains("runCoreRoutedPath(srcPath, destPath, srcRouting, destRouting)"));

    const QString deletePath = QFINDTESTDATA("../libfm-qt/src/core/deletejob.cpp");
    QVERIFY2(!deletePath.isEmpty(), "Unable to locate libfm-qt/src/core/deletejob.cpp");
    QFile deleteSource(deletePath);
    QVERIFY(deleteSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray deleteBytes = deleteSource.readAll();

    QVERIFY(deleteBytes.contains("classifyPathForFileOps(path)"));
    QVERIFY(deleteBytes.contains("RoutingClass::CoreLocal"));
    QVERIFY(deleteBytes.contains("RoutingClass::LegacyGio"));
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
    QVERIFY(trashBytes.contains("CoreFileOps::run(request)"));

    const QString untrashPath = QFINDTESTDATA("../libfm-qt/src/core/untrashjob.cpp");
    QVERIFY2(!untrashPath.isEmpty(), "Unable to locate libfm-qt/src/core/untrashjob.cpp");
    QFile untrashSource(untrashPath);
    QVERIFY(untrashSource.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray untrashBytes = untrashSource.readAll();
    QVERIFY(untrashBytes.contains("CoreFileOps::UntrashRequest request"));
    QVERIFY(untrashBytes.contains("FileOpsRequestAssembly::buildUntrashRequest"));
    QVERIFY(untrashBytes.contains("FileOpsRequestAssembly::validateRequestPathCount"));
    QVERIFY(untrashBytes.contains("CoreFileOps::run(request, handlers)"));
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
    GErrorPtr err;
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

QTEST_MAIN(LibfmQtFileOpsBridgeTest)

#include "libfmqt_fileops_bridge_test.moc"
