/*
 * Tests for Qt file operations adapter using the POSIX core
 * tests/qt_fileops_test.cpp
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "../src/backends/qt/qt_fileops.h"

#include <QFile>
#include <QFileInfo>
#include <fcntl.h>
#include <unistd.h>

using namespace Oneg4FM;

class QtFileOpsTest : public QObject {
    Q_OBJECT

   private slots:
    void copyFile();
    void copyProgressIsMonotonicAndTotalsStable();
    void copyConflictRespectsOverwriteExistingFlag();
    void moveFile();
    void deleteFile();
    void deleteProgressAggregatesAcrossSources();
    void deleteDirectoryProgressUsesRecursiveCounts();
    void deleteRejectsDestinationField();
    void deleteRejectsOverwriteExistingField();
    void copyRejectsFollowSymlinks();
    void copyUriSourceRoutesViaCoreGioBackend();
    void copyPromptConflictUsesUiResolution();
    void copyPromptConflictRequiresResponder();
    void copyRejectsPromptAndOverwriteTogether();
    void deleteRejectsPromptOnConflictField();
    void cancelRequestsStopCopyThroughCoreContract();
    void requestFieldsAreMappedOrRejectedExplicitly();
    void adapterRemainsThinAndDelegatesPlanningToCore();
};

static QString writeTempFile(const QTemporaryDir& dir, const QString& name, const QByteArray& data) {
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    return path;
}

static QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return f.readAll();
}

static bool createSparseFile(const QString& path, qint64 sizeBytes) {
    const QByteArray native = QFile::encodeName(path);
    int fd = ::open(native.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }

    const bool ok = (::ftruncate(fd, static_cast<off_t>(sizeBytes)) == 0);
    ::close(fd);
    return ok;
}

void QtFileOpsTest::copyFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "copy-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    const QString copied = dstDir + QLatin1String("/src.txt");
    QVERIFY(QFileInfo::exists(copied));
    QCOMPARE(QFile(copied).size(), QFile(src).size());
}

void QtFileOpsTest::copyProgressIsMonotonicAndTotalsStable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = dir.path() + QLatin1String("/large-sparse.bin");
    QVERIFY(createSparseFile(src, qint64(64) * 1024 * 1024));

    const QString dstDir = dir.path() + QLatin1String("/dst-progress");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/large-sparse.bin");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);
    QVector<FileOpProgress> updates;
    connect(&ops, &QtFileOps::progress, &ops, [&updates](const FileOpProgress& info) { updates.push_back(info); });

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = true;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 10000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QVERIFY(QFileInfo::exists(dstPath));
    QVERIFY(!updates.isEmpty());
    QVERIFY2(updates.size() < 300, "Progress updates should be coalesced to avoid starving finished delivery");

    std::uint64_t prevBytesDone = 0;
    bool hasPrevBytesDone = false;
    std::uint64_t stableBytesTotal = 0;
    bool hasStableBytesTotal = false;
    int prevFilesDone = -1;
    int stableFilesTotal = -1;
    FileOpProgress prevSnapshot{};
    bool hasPrevSnapshot = false;
    bool sawFinalizing = false;
    for (const FileOpProgress& info : updates) {
        if (hasPrevSnapshot) {
            QVERIFY(!(info.bytesDone == prevSnapshot.bytesDone && info.bytesTotal == prevSnapshot.bytesTotal &&
                      info.filesDone == prevSnapshot.filesDone && info.filesTotal == prevSnapshot.filesTotal &&
                      info.currentPath == prevSnapshot.currentPath && info.phase == prevSnapshot.phase));
        }
        prevSnapshot = info;
        hasPrevSnapshot = true;
        if (info.phase == FileOpProgressPhase::Finalizing) {
            sawFinalizing = true;
        }

        if (hasPrevBytesDone) {
            QVERIFY(info.bytesDone >= prevBytesDone);
        }
        hasPrevBytesDone = true;
        prevBytesDone = info.bytesDone;

        QVERIFY(info.bytesTotal >= info.bytesDone);
        if (info.bytesTotal > 0) {
            if (!hasStableBytesTotal) {
                stableBytesTotal = info.bytesTotal;
                hasStableBytesTotal = true;
            }
            else {
                QCOMPARE(info.bytesTotal, stableBytesTotal);
            }
        }

        QVERIFY(info.filesDone >= prevFilesDone);
        prevFilesDone = info.filesDone;

        if (stableFilesTotal < 0) {
            stableFilesTotal = info.filesTotal;
        }
        else {
            QCOMPARE(info.filesTotal, stableFilesTotal);
        }
    }

    const FileOpProgress& last = updates.constLast();
    QVERIFY(sawFinalizing);
    QCOMPARE(last.filesTotal, 1);
    QCOMPARE(last.filesDone, 1);
    QVERIFY(last.bytesDone > 0);
    QCOMPARE(last.bytesDone, last.bytesTotal);
    QCOMPARE(last.phase, FileOpProgressPhase::Finalizing);
}

void QtFileOpsTest::copyConflictRespectsOverwriteExistingFlag() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "new-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = writeTempFile(dir, QStringLiteral("dst/src.txt"), "old-data");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QCOMPARE(readFile(dstPath), QByteArray("old-data"));

    req.overwriteExisting = true;
    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QCOMPARE(readFile(dstPath), QByteArray("new-data"));
}

void QtFileOpsTest::moveFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("move.txt"), "move-data");
    const QString dstDir = dir.path() + QLatin1String("/dst2");
    QVERIFY(QDir().mkpath(dstDir));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Move;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    const QString moved = dstDir + QLatin1String("/move.txt");
    QVERIFY(QFileInfo::exists(moved));
    QVERIFY(!QFileInfo::exists(src));
}

void QtFileOpsTest::deleteFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("delete.txt"), "delete-data");
    QVERIFY(QFileInfo::exists(src));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = QStringList{src};
    req.destination.clear();
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QVERIFY(!QFileInfo::exists(src));
}

void QtFileOpsTest::deleteProgressAggregatesAcrossSources() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString first = writeTempFile(dir, QStringLiteral("first.txt"), "first");
    const QString second = writeTempFile(dir, QStringLiteral("second.txt"), "second");
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);
    QVector<FileOpProgress> updates;
    connect(&ops, &QtFileOps::progress, &ops, [&updates](const FileOpProgress& info) { updates.push_back(info); });

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = QStringList{first, second};
    req.destination.clear();
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QVERIFY(!QFileInfo::exists(first));
    QVERIFY(!QFileInfo::exists(second));
    QVERIFY(!updates.isEmpty());

    int previousDone = -1;
    bool sawTotalAcrossRequest = false;
    for (const FileOpProgress& info : updates) {
        QVERIFY(!info.currentPath.isEmpty());
        QVERIFY(info.filesDone >= previousDone);
        previousDone = info.filesDone;
        if (info.filesTotal == 2) {
            sawTotalAcrossRequest = true;
        }
    }

    QVERIFY(sawTotalAcrossRequest);
    const FileOpProgress& last = updates.constLast();
    QCOMPARE(last.filesTotal, 2);
    QCOMPARE(last.filesDone, 2);
}

void QtFileOpsTest::deleteDirectoryProgressUsesRecursiveCounts() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString treeRoot = dir.path() + QLatin1String("/tree");
    QVERIFY(QDir().mkpath(treeRoot + QLatin1String("/nested")));
    const QString fileA = writeTempFile(dir, QStringLiteral("tree/root.txt"), "root");
    const QString fileB = writeTempFile(dir, QStringLiteral("tree/nested/child.txt"), "child");
    QVERIFY(QFileInfo::exists(fileA));
    QVERIFY(QFileInfo::exists(fileB));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);
    QVector<FileOpProgress> updates;
    connect(&ops, &QtFileOps::progress, &ops, [&updates](const FileOpProgress& info) { updates.push_back(info); });

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = QStringList{treeRoot};
    req.destination.clear();
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QVERIFY(!QFileInfo::exists(treeRoot));
    QVERIFY(!updates.isEmpty());

    int maxDone = 0;
    bool sawRecursiveTotal = false;
    for (const FileOpProgress& info : updates) {
        QVERIFY(!info.currentPath.isEmpty());
        maxDone = qMax(maxDone, info.filesDone);
        if (info.filesTotal >= 4) {
            sawRecursiveTotal = true;
        }
    }

    QVERIFY(sawRecursiveTotal);
    QVERIFY(maxDone >= 2);
    const FileOpProgress& last = updates.constLast();
    QVERIFY(last.filesTotal >= 4);
    QCOMPARE(last.filesDone, last.filesTotal);
}

void QtFileOpsTest::deleteRejectsDestinationField() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("delete.txt"), "delete-data");
    QVERIFY(QFileInfo::exists(src));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = QStringList{src};
    req.destination = dir.path() + QLatin1String("/ignored");
    req.followSymlinks = false;
    req.overwriteExisting = false;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("destination"), Qt::CaseInsensitive));
    QVERIFY(QFileInfo::exists(src));
}

void QtFileOpsTest::deleteRejectsOverwriteExistingField() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("delete.txt"), "delete-data");
    QVERIFY(QFileInfo::exists(src));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = QStringList{src};
    req.destination.clear();
    req.followSymlinks = false;
    req.overwriteExisting = true;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("overwriteExisting"), Qt::CaseInsensitive));
    QVERIFY(QFileInfo::exists(src));
}

void QtFileOpsTest::copyRejectsFollowSymlinks() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "copy-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/src.txt");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = true;
    req.overwriteExisting = false;
    req.promptOnConflict = false;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("followSymlinks"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(dstPath));
}

void QtFileOpsTest::copyUriSourceRoutesViaCoreGioBackend() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "gio-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/src.txt");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{QUrl::fromLocalFile(src).toString()};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.promptOnConflict = false;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QVERIFY(args.at(1).toString().isEmpty());
    QVERIFY(QFileInfo::exists(dstPath));
    QCOMPARE(readFile(dstPath), QByteArray("gio-data"));
}

void QtFileOpsTest::copyPromptConflictUsesUiResolution() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "new-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = writeTempFile(dir, QStringLiteral("dst/src.txt"), "old-data");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    int conflictCount = 0;
    FileOpConflict observedConflict;
    connect(&ops, &QtFileOps::conflictRequested, &ops,
            [&ops, &conflictCount, &observedConflict](const FileOpConflict& info) {
                ++conflictCount;
                observedConflict = info;
                ops.resolveConflict(FileOpConflictResolution::Rename);
            });

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.promptOnConflict = true;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(args.at(0).toBool());
    QCOMPARE(conflictCount, 1);
    QCOMPARE(observedConflict.sourcePath, src);
    QCOMPARE(observedConflict.destinationPath, dstPath);
    QVERIFY(!observedConflict.destinationIsDirectory);
    QCOMPARE(readFile(dstPath), QByteArray("old-data"));

    const QString renamedPath = dstDir + QLatin1String("/src (copy).txt");
    QVERIFY(QFileInfo::exists(renamedPath));
    QCOMPARE(readFile(renamedPath), QByteArray("new-data"));
}

void QtFileOpsTest::copyPromptConflictRequiresResponder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "new-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = writeTempFile(dir, QStringLiteral("dst/src.txt"), "old-data");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.promptOnConflict = true;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("conflict responder"), Qt::CaseInsensitive));
    QCOMPARE(readFile(dstPath), QByteArray("old-data"));
}

void QtFileOpsTest::copyRejectsPromptAndOverwriteTogether() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("src.txt"), "copy-data");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/src.txt");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);
    connect(&ops, &QtFileOps::conflictRequested, &ops,
            [&ops](const FileOpConflict&) { ops.resolveConflict(FileOpConflictResolution::Rename); });

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = true;
    req.promptOnConflict = true;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("promptOnConflict"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(dstPath));
}

void QtFileOpsTest::deleteRejectsPromptOnConflictField() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir, QStringLiteral("delete.txt"), "delete-data");
    QVERIFY(QFileInfo::exists(src));

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    FileOpRequest req;
    req.type = FileOpType::Delete;
    req.sources = QStringList{src};
    req.destination.clear();
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.promptOnConflict = true;

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("promptOnConflict"), Qt::CaseInsensitive));
    QVERIFY(QFileInfo::exists(src));
}

void QtFileOpsTest::cancelRequestsStopCopyThroughCoreContract() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = dir.path() + QLatin1String("/large-sparse.bin");
    QVERIFY(createSparseFile(src, qint64(2) * 1024 * 1024 * 1024));

    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/large-sparse.bin");

    QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &QtFileOps::finished);

    bool cancelRequested = false;
    connect(&ops, &QtFileOps::progress, &ops, [&ops, &cancelRequested](const FileOpProgress& info) {
        if (cancelRequested || info.bytesDone == 0) {
            return;
        }
        cancelRequested = true;
        ops.cancel();
    });

    FileOpRequest req;
    req.type = FileOpType::Copy;
    req.sources = QStringList{src};
    req.destination = dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = true;

    ops.start(req);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 15000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(cancelRequested);
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(dstPath));
}

void QtFileOpsTest::requestFieldsAreMappedOrRejectedExplicitly() {
    const QString adapterSourcePath = QFINDTESTDATA("../src/backends/qt/qt_fileops.cpp");
    QVERIFY2(!adapterSourcePath.isEmpty(), "Unable to locate src/backends/qt/qt_fileops.cpp from test");

    QFile adapterSource(adapterSourcePath);
    QVERIFY(adapterSource.open(QIODevice::ReadOnly));
    const QByteArray source = adapterSource.readAll();

    const QList<QByteArray> requiredSnippets = {
        QByteArray("toContractOperation(req.type, out.operation)"),
        QByteArray("out.sources.push_back(toNativePath(source))"),
        QByteArray("out.routing.sourceKinds.push_back(toContractEndpointKind(source))"),
        QByteArray("out.destination.targetDir = toNativePath(req.destination)"),
        QByteArray("out.routing.destinationKind = toContractEndpointKind(req.destination)"),
        QByteArray("requestError = QStringLiteral(\"Delete operations do not accept a destination path\")"),
        QByteArray("requestError = QStringLiteral(\"Delete operations do not use overwriteExisting\")"),
        QByteArray("requestError = QStringLiteral(\"Delete operations do not use promptOnConflict\")"),
        QByteArray("requestError = QStringLiteral(\"promptOnConflict cannot be combined with overwriteExisting\")"),
        QByteArray("out.conflictPolicy = req.overwriteExisting ? FileOpsContract::ConflictPolicy::Overwrite"),
        QByteArray("requestError = QStringLiteral(\"followSymlinks=true is not supported\")"),
        QByteArray("out.metadata.preserveOwnership = req.preserveOwnership"),
        QByteArray("const FileOpsContract::Result preflightResult = FileOpsContract::preflight(contractReq)"),
    };

    for (const QByteArray& snippet : requiredSnippets) {
        QVERIFY2(source.contains(snippet),
                 qPrintable(QStringLiteral("qt_fileops.cpp is missing required request-field handling snippet: %1")
                                .arg(QString::fromLocal8Bit(snippet))));
    }
}

void QtFileOpsTest::adapterRemainsThinAndDelegatesPlanningToCore() {
    const QString adapterSourcePath = QFINDTESTDATA("../src/backends/qt/qt_fileops.cpp");
    QVERIFY2(!adapterSourcePath.isEmpty(), "Unable to locate src/backends/qt/qt_fileops.cpp from test");

    QFile adapterSource(adapterSourcePath);
    QVERIFY(adapterSource.open(QIODevice::ReadOnly));
    const QByteArray source = adapterSource.readAll();

    QVERIFY(source.contains("FileOpsContract::run(contractReq, handlers)"));

    // Keep planning/scan semantics centralized in src/core.
    QVERIFY(!source.contains("QDirIterator"));
    QVERIFY(!source.contains("QFileInfo"));
    QVERIFY(!source.contains("scan_path_stats"));
    QVERIFY(!source.contains("g_file_enumerate_children"));
}

QTEST_MAIN(QtFileOpsTest)
#include "qt_fileops_test.moc"
