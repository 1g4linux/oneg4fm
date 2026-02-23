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
#include "../libfm-qt/src/core/filetransferjob.h"

#include <cstdint>

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
    void deleteNativeDirectoryTreeTracksCompletion();
    void coreRoutingEligibilityAcceptsNativeLocalPath();
    void coreRoutingEligibilityRejectsUriSchemes();
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

QTEST_MAIN(LibfmQtFileOpsBridgeTest)

#include "libfmqt_fileops_bridge_test.moc"
