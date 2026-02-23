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
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

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
    void moveReportsMonotonicProgressAndStableTotals();
    void deleteReportsMonotonicProgressAndStableTotals();
    void cancelReturnsEcanceled();
    void moveCancelAfterProgressReturnsEcanceledWithFinalSnapshot();
    void skipConflictPolicyHandlesLateDestinationConflict();
    void renameConflictPolicyCreatesUniqueDestination();
    void promptConflictCanSkipWithStructuredEvent();
    void promptConflictOverwriteReplacesDestinationForSymlinkSource();
    void promptConflictCanRenameWithStructuredEvent();
    void promptConflictSkipAllAppliesAcrossSources();
    void promptConflictRenameAllAppliesAcrossSources();
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

void FileOpsContractTest::moveReportsMonotonicProgressAndStableTotals() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = writeTempFile(dir.path() + QLatin1String("/source.bin"), QByteArray("payload-data"));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));

    Request req;
    req.operation = Operation::Move;
    req.sources = {toNative(srcPath)};
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

    const QString movedPath = dstDir + QLatin1String("/source.bin");
    QVERIFY(!QFileInfo::exists(srcPath));
    QVERIFY(QFileInfo::exists(movedPath));
    QCOMPARE(readFile(movedPath), QByteArray("payload-data"));

    QVERIFY(!updates.isEmpty());
    int prevFilesDone = -1;
    std::uint64_t prevBytesDone = 0;
    for (const ProgressSnapshot& update : updates) {
        QCOMPARE(update.filesTotal, 1);
        QCOMPARE(update.bytesTotal, std::uint64_t(12));
        QVERIFY(update.filesDone >= prevFilesDone);
        QVERIFY(update.bytesDone >= prevBytesDone);
        prevFilesDone = update.filesDone;
        prevBytesDone = update.bytesDone;
    }

    QCOMPARE(result.finalProgress.filesTotal, 1);
    QCOMPARE(result.finalProgress.filesDone, 1);
    QCOMPARE(result.finalProgress.bytesTotal, std::uint64_t(12));
    QCOMPARE(result.finalProgress.bytesDone, std::uint64_t(12));
}

void FileOpsContractTest::deleteReportsMonotonicProgressAndStableTotals() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcRoot = dir.path() + QLatin1String("/to-delete");
    QVERIFY(QDir().mkpath(srcRoot + QLatin1String("/nested")));
    const QString rootFile = writeTempFile(srcRoot + QLatin1String("/root.txt"), QByteArray("root"));
    const QString nestedFile = writeTempFile(srcRoot + QLatin1String("/nested/child.txt"), QByteArray("child-data"));
    QVERIFY(QFileInfo::exists(rootFile));
    QVERIFY(QFileInfo::exists(nestedFile));

    Request req;
    req.operation = Operation::Delete;
    req.sources = {toNative(srcRoot)};
    req.conflictPolicy = ConflictPolicy::Overwrite;

    QVector<ProgressSnapshot> updates;
    EventHandlers handlers;
    handlers.onProgress = [&updates](const ProgressSnapshot& info) { updates.push_back(info); };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QVERIFY(result.error.code == EngineErrorCode::None);
    QVERIFY(!QFileInfo::exists(srcRoot));

    QVERIFY(!updates.isEmpty());
    QVERIFY(result.finalProgress.filesTotal >= 4);
    int prevFilesDone = -1;
    std::uint64_t prevBytesDone = 0;
    for (const ProgressSnapshot& update : updates) {
        QCOMPARE(update.filesTotal, result.finalProgress.filesTotal);
        QCOMPARE(update.bytesTotal, result.finalProgress.bytesTotal);
        QVERIFY(update.filesDone >= prevFilesDone);
        QVERIFY(update.bytesDone >= prevBytesDone);
        prevFilesDone = update.filesDone;
        prevBytesDone = update.bytesDone;
    }

    QCOMPARE(result.finalProgress.filesDone, result.finalProgress.filesTotal);
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
    QCOMPARE(result.finalProgress.filesTotal, 1);
    QCOMPARE(result.finalProgress.bytesTotal, std::uint64_t(payload.size()));
    QVERIFY(result.finalProgress.filesDone <= 1);
    QVERIFY(result.finalProgress.bytesDone > 0);
    QVERIFY(result.finalProgress.bytesDone < result.finalProgress.bytesTotal);
    QVERIFY(!result.finalProgress.currentPath.empty());

    const QString dstPath = dstDir + QLatin1String("/big.bin");
    QVERIFY(!QFileInfo::exists(dstPath));
}

void FileOpsContractTest::moveCancelAfterProgressReturnsEcanceledWithFinalSnapshot() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = writeTempFile(dir.path() + QLatin1String("/move-source.txt"), QByteArray("move-me"));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/move-source.txt");

    std::atomic<bool> cancelRequested{false};

    Request req;
    req.operation = Operation::Move;
    req.sources = {toNative(srcPath)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Overwrite;
    req.cancellationRequested = [&cancelRequested]() { return cancelRequested.load(); };

    bool sawProgress = false;
    EventHandlers handlers;
    handlers.onProgress = [&cancelRequested, &sawProgress](const ProgressSnapshot& update) {
        sawProgress = true;
        if (update.filesDone > 0) {
            cancelRequested.store(true);
        }
    };

    const Result result = run(req, handlers);
    QVERIFY(!result.success);
    QVERIFY(result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::Cancelled);
    QCOMPARE(result.error.sysErrno, ECANCELED);
    QVERIFY(sawProgress);
    QCOMPARE(result.finalProgress.filesTotal, 1);
    QCOMPARE(result.finalProgress.filesDone, 1);
    QCOMPARE(result.finalProgress.bytesTotal, std::uint64_t(7));
    QCOMPARE(result.finalProgress.bytesDone, std::uint64_t(0));
    QVERIFY(!result.finalProgress.currentPath.empty());

    QVERIFY(QFileInfo::exists(dstPath));
    QVERIFY(!QFileInfo::exists(srcPath));
}

void FileOpsContractTest::skipConflictPolicyHandlesLateDestinationConflict() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QByteArray largePayload(8 * 1024 * 1024, 'a');
    const QString first = writeTempFile(srcDir + QLatin1String("/first.bin"), largePayload);
    const QString second = writeTempFile(srcDir + QLatin1String("/second.txt"), QByteArray("source-second"));
    const QString secondDest = dstDir + QLatin1String("/second.txt");

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(first), toNative(second)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Skip;

    bool injectedLateConflict = false;
    EventHandlers handlers;
    handlers.onProgress = [&](const ProgressSnapshot& update) {
        if (injectedLateConflict) {
            return;
        }
        if (update.currentPath != toNative(first) || update.bytesDone == 0) {
            return;
        }

        QFile injected(secondDest);
        QVERIFY(injected.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(injected.write("late-existing"), qint64(13));
        injected.close();
        injectedLateConflict = true;
    };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QVERIFY(injectedLateConflict);

    QCOMPARE(readFile(dstDir + QLatin1String("/first.bin")), largePayload);
    QCOMPARE(readFile(secondDest), QByteArray("late-existing"));
    QCOMPARE(result.finalProgress.filesTotal, 2);
    QCOMPARE(result.finalProgress.filesDone, 2);
}

void FileOpsContractTest::renameConflictPolicyCreatesUniqueDestination() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcRoot = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcRoot + QLatin1String("/a")));
    QVERIFY(QDir().mkpath(srcRoot + QLatin1String("/b")));
    QVERIFY(QDir().mkpath(dstDir));

    const QString srcFirst = writeTempFile(srcRoot + QLatin1String("/a/item.txt"), QByteArray("new-a"));
    const QString srcSecond = writeTempFile(srcRoot + QLatin1String("/b/item.txt"), QByteArray("new-b"));
    const QString existing = writeTempFile(dstDir + QLatin1String("/item.txt"), QByteArray("old"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(srcFirst), toNative(srcSecond)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Rename;

    const Result result = run(req);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(readFile(existing), QByteArray("old"));
    QCOMPARE(readFile(dstDir + QLatin1String("/item (copy).txt")), QByteArray("new-a"));
    QCOMPARE(readFile(dstDir + QLatin1String("/item (copy 2).txt")), QByteArray("new-b"));
    QCOMPARE(result.finalProgress.filesTotal, 2);
    QCOMPARE(result.finalProgress.filesDone, 2);
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

void FileOpsContractTest::promptConflictOverwriteReplacesDestinationForSymlinkSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString linkTarget = writeTempFile(srcDir + QLatin1String("/target.txt"), QByteArray("target-data"));
    const QString srcLink = srcDir + QLatin1String("/item-link");
    QVERIFY(::symlink(linkTarget.toLocal8Bit().constData(), srcLink.toLocal8Bit().constData()) == 0);

    const QString existing = writeTempFile(dstDir + QLatin1String("/item-link"), QByteArray("old-content"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(srcLink)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Prompt;

    int conflictCount = 0;
    EventHandlers handlers;
    handlers.onConflict = [&conflictCount](const ConflictEvent&) -> ConflictResolution {
        ++conflictCount;
        return ConflictResolution::Overwrite;
    };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(conflictCount, 1);

    struct stat st{};
    QVERIFY(::lstat(existing.toLocal8Bit().constData(), &st) == 0);
    QVERIFY(S_ISLNK(st.st_mode));

    char linkBuf[PATH_MAX];
    const ssize_t linkLen = ::readlink(existing.toLocal8Bit().constData(), linkBuf, sizeof(linkBuf) - 1);
    QVERIFY(linkLen > 0);
    linkBuf[linkLen] = '\0';
    QCOMPARE(QString::fromLocal8Bit(linkBuf), linkTarget);
}

void FileOpsContractTest::promptConflictCanRenameWithStructuredEvent() {
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
    EventHandlers handlers;
    handlers.onConflict = [&conflictCount](const ConflictEvent&) -> ConflictResolution {
        ++conflictCount;
        return ConflictResolution::Rename;
    };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(conflictCount, 1);
    QCOMPARE(readFile(existing), QByteArray("old-content"));
    QCOMPARE(readFile(dstDir + QLatin1String("/item (copy).txt")), QByteArray("new-content"));
}

void FileOpsContractTest::promptConflictSkipAllAppliesAcrossSources() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString firstSrc = writeTempFile(srcDir + QLatin1String("/first.txt"), QByteArray("new-first"));
    const QString secondSrc = writeTempFile(srcDir + QLatin1String("/second.txt"), QByteArray("new-second"));
    const QString firstDst = writeTempFile(dstDir + QLatin1String("/first.txt"), QByteArray("old-first"));
    const QString secondDst = writeTempFile(dstDir + QLatin1String("/second.txt"), QByteArray("old-second"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(firstSrc), toNative(secondSrc)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Prompt;

    int conflictCount = 0;
    EventHandlers handlers;
    handlers.onConflict = [&conflictCount](const ConflictEvent&) -> ConflictResolution {
        ++conflictCount;
        return ConflictResolution::SkipAll;
    };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(conflictCount, 1);
    QCOMPARE(readFile(firstDst), QByteArray("old-first"));
    QCOMPARE(readFile(secondDst), QByteArray("old-second"));
    QCOMPARE(result.finalProgress.filesTotal, 2);
    QCOMPARE(result.finalProgress.filesDone, 2);
}

void FileOpsContractTest::promptConflictRenameAllAppliesAcrossSources() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString firstSrc = writeTempFile(srcDir + QLatin1String("/first.txt"), QByteArray("new-first"));
    const QString secondSrc = writeTempFile(srcDir + QLatin1String("/second.txt"), QByteArray("new-second"));
    const QString firstDst = writeTempFile(dstDir + QLatin1String("/first.txt"), QByteArray("old-first"));
    const QString secondDst = writeTempFile(dstDir + QLatin1String("/second.txt"), QByteArray("old-second"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(firstSrc), toNative(secondSrc)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Prompt;

    int conflictCount = 0;
    EventHandlers handlers;
    handlers.onConflict = [&conflictCount](const ConflictEvent&) -> ConflictResolution {
        ++conflictCount;
        return ConflictResolution::RenameAll;
    };

    const Result result = run(req, handlers);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(conflictCount, 1);
    QCOMPARE(readFile(firstDst), QByteArray("old-first"));
    QCOMPARE(readFile(secondDst), QByteArray("old-second"));
    QCOMPARE(readFile(dstDir + QLatin1String("/first (copy).txt")), QByteArray("new-first"));
    QCOMPARE(readFile(dstDir + QLatin1String("/second (copy).txt")), QByteArray("new-second"));
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
