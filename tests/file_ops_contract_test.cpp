/*
 * Tests for unified file-operations contract executor
 * tests/file_ops_contract_test.cpp
 */

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "../src/core/file_ops_contract.h"

#include <atomic>
#include <cerrno>

using namespace PCManFM::FileOpsContract;

namespace {

std::string toNative(const QString& path) {
    const QByteArray bytes = QFile::encodeName(path);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString writeTempFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    return path;
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return f.readAll();
}

}  // namespace

class FileOpsContractTest : public QObject {
    Q_OBJECT

   private slots:
    void copyReportsMonotonicProgressAndStableTotals();
    void cancelReturnsEcanceled();
    void promptConflictCanSkipWithStructuredEvent();
    void unsupportedOperationRejected();
    void requireLandlockFailsFast();
};

void FileOpsContractTest::copyReportsMonotonicProgressAndStableTotals() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString first = writeTempFile(srcDir + QLatin1String("/first.txt"), QByteArray("first-data"));
    const QString second = writeTempFile(srcDir + QLatin1String("/second.txt"), QByteArray("second-data"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(first), toNative(second)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Overwrite;

    QVector<ProgressSnapshot> updates;
    EventHandlers handlers;
    handlers.onProgress = [&updates](const ProgressSnapshot& info) { updates.push_back(info); };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QVERIFY(result.error.code == EngineErrorCode::None);

    QVERIFY(QFileInfo::exists(dstDir + QLatin1String("/first.txt")));
    QVERIFY(QFileInfo::exists(dstDir + QLatin1String("/second.txt")));

    QVERIFY(!updates.isEmpty());
    int prevFilesDone = -1;
    std::uint64_t prevBytesDone = 0;
    for (const ProgressSnapshot& update : updates) {
        QCOMPARE(update.filesTotal, 2);
        QVERIFY(update.filesDone >= prevFilesDone);
        QVERIFY(update.bytesDone >= prevBytesDone);
        prevFilesDone = update.filesDone;
        prevBytesDone = update.bytesDone;
    }

    QCOMPARE(result.finalProgress.filesTotal, 2);
    QCOMPARE(result.finalProgress.filesDone, 2);
    QCOMPARE(result.finalProgress.bytesDone, result.finalProgress.bytesTotal);
}

void FileOpsContractTest::cancelReturnsEcanceled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = dir.path() + QLatin1String("/big.bin");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));

    QByteArray payload(8 * 1024 * 1024, 'x');
    QVERIFY(!writeTempFile(srcPath, payload).isEmpty());

    std::atomic<bool> cancelRequested{false};

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(srcPath)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Overwrite;
    req.cancellationRequested = [&cancelRequested]() { return cancelRequested.load(); };

    EventHandlers handlers;
    handlers.onProgress = [&cancelRequested](const ProgressSnapshot& update) {
        if (update.bytesDone > 0) {
            cancelRequested.store(true);
        }
    };

    const Result result = run(req, handlers);
    QVERIFY(!result.success);
    QVERIFY(result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::Cancelled);
    QCOMPARE(result.error.sysErrno, ECANCELED);

    const QString dstPath = dstDir + QLatin1String("/big.bin");
    QVERIFY(!QFileInfo::exists(dstPath));
}

void FileOpsContractTest::promptConflictCanSkipWithStructuredEvent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString src = writeTempFile(srcDir + QLatin1String("/item.txt"), QByteArray("new-content"));
    const QString existing = writeTempFile(dstDir + QLatin1String("/item.txt"), QByteArray("old-content"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(src)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Prompt;

    int conflictCount = 0;
    bool conflictPayloadMatches = true;
    EventHandlers handlers;
    handlers.onConflict = [&conflictCount, &src, &existing,
                           &conflictPayloadMatches](const ConflictEvent& event) -> ConflictResolution {
        ++conflictCount;
        if (event.kind != ConflictKind::DestinationExists) {
            conflictPayloadMatches = false;
        }
        if (QString::fromLocal8Bit(event.sourcePath.c_str()) != src) {
            conflictPayloadMatches = false;
        }
        if (QString::fromLocal8Bit(event.destinationPath.c_str()) != existing) {
            conflictPayloadMatches = false;
        }
        return ConflictResolution::Skip;
    };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(conflictCount, 1);
    QVERIFY(conflictPayloadMatches);
    QCOMPARE(readFile(existing), QByteArray("old-content"));

    QCOMPARE(result.finalProgress.filesTotal, 1);
    QCOMPARE(result.finalProgress.filesDone, 1);
}

void FileOpsContractTest::unsupportedOperationRejected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir.path() + QLatin1String("/src.txt"), QByteArray("x"));

    Request req;
    req.operation = Operation::Link;
    req.sources = {toNative(src)};

    const Result result = run(req);
    QVERIFY(!result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::UnsupportedOperation);
}

void FileOpsContractTest::requireLandlockFailsFast() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir.path() + QLatin1String("/src.txt"), QByteArray("x"));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(src)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.linuxSafety.requireLandlock = true;

    const Result result = run(req);
    QVERIFY(!result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::SafetyRequirementUnavailable);
    QCOMPARE(result.error.sysErrno, ENOSYS);
}

QTEST_MAIN(FileOpsContractTest)
#include "file_ops_contract_test.moc"
