/*
 * Tests for Qt file operations adapter using the POSIX core
 * tests/qt_fileops_test.cpp
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../src/backends/qt/qt_fileops.h"

#include <QFile>
#include <QFileInfo>

using namespace PCManFM;

class QtFileOpsTest : public QObject {
    Q_OBJECT

   private slots:
    void copyFile();
    void copyConflictRespectsOverwriteExistingFlag();
    void moveFile();
    void deleteFile();
    void deleteProgressAggregatesAcrossSources();
    void deleteDirectoryProgressUsesRecursiveCounts();
    void deleteRejectsDestinationField();
    void deleteRejectsOverwriteExistingField();
    void copyRejectsFollowSymlinks();
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

    ops.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 2000);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(args.at(1).toString().contains(QStringLiteral("followSymlinks"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(dstPath));
}

QTEST_MAIN(QtFileOpsTest)
#include "qt_fileops_test.moc"
