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
#include <QUrl>

#include "../src/core/file_ops_contract.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Oneg4FM::FileOpsContract;

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

bool createSparseFile(const QString& path, qint64 sizeBytes) {
    const QByteArray native = QFile::encodeName(path);
    int fd = ::open(native.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }

    const bool ok = (::ftruncate(fd, static_cast<off_t>(sizeBytes)) == 0);
    ::close(fd);
    return ok;
}

class ScopedEnvVar {
   public:
    explicit ScopedEnvVar(const char* name, const char* value) : name_(name) {
        const char* current = ::getenv(name_);
        if (current) {
            hadOriginal_ = true;
            original_ = current;
        }

        if (value) {
            ::setenv(name_, value, 1);
        }
        else {
            ::unsetenv(name_);
        }
    }

    ~ScopedEnvVar() {
        if (hadOriginal_) {
            ::setenv(name_, original_.c_str(), 1);
        }
        else {
            ::unsetenv(name_);
        }
    }

   private:
    const char* name_;
    std::string original_;
    bool hadOriginal_ = false;
};

}  // namespace

class FileOpsContractTest : public QObject {
    Q_OBJECT

   private slots:
    void copyReportsMonotonicProgressAndStableTotals();
    void copyWorksWhenPathsUseSymlinkedDirectoryAlias();
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
    void sandboxedThreadCopyMatchesInProcessBehavior();
    void sandboxedThreadPromptConflictMatchesInProcessBehavior();
    void sandboxedThreadCancelReturnsEcanceled();
    void unsupportedOperationRejected();
    void capabilityReportExposesLocalAndGioBackends();
    void routingSourceKindSizeMismatchRejected();
    void uriSourceCanRunViaGioBackend();
    void forcingLocalBackendForUriIsRejected();
    void trashAndUntrashPreflightOnGioBackend();
    void requireLandlockFailsFast();
    void requireOpenat2ResolveFailsFastWhenUnavailable();
    void requireSeccompNeedsSandboxedWorkerMode();
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
    bool sawFinalizing = false;
    for (const ProgressSnapshot& update : updates) {
        QCOMPARE(update.filesTotal, 2);
        QVERIFY(update.filesDone >= prevFilesDone);
        QVERIFY(update.bytesDone >= prevBytesDone);
        if (update.phase == ProgressPhase::Finalizing) {
            sawFinalizing = true;
        }
        prevFilesDone = update.filesDone;
        prevBytesDone = update.bytesDone;
    }

    QVERIFY(sawFinalizing);
    QCOMPARE(result.finalProgress.filesTotal, 2);
    QCOMPARE(result.finalProgress.filesDone, 2);
    QCOMPARE(result.finalProgress.bytesDone, result.finalProgress.bytesTotal);
    QCOMPARE(result.finalProgress.phase, ProgressPhase::Finalizing);
}

void FileOpsContractTest::copyWorksWhenPathsUseSymlinkedDirectoryAlias() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString realRoot = dir.path() + QLatin1String("/real");
    const QString realSrcDir = realRoot + QLatin1String("/src");
    const QString realDstDir = realRoot + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(realSrcDir));
    QVERIFY(QDir().mkpath(realDstDir));

    const QString aliasRoot = dir.path() + QLatin1String("/alias");
    QVERIFY(::symlink(realRoot.toLocal8Bit().constData(), aliasRoot.toLocal8Bit().constData()) == 0);

    const QString aliasedSource = writeTempFile(aliasRoot + QLatin1String("/src/item.txt"), QByteArray("aliased"));
    QCOMPARE(readFile(aliasedSource), QByteArray("aliased"));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(aliasedSource)};
    req.destination.targetDir = toNative(aliasRoot + QLatin1String("/dst"));
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Overwrite;

    const Result result = run(req);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::None);
    QCOMPARE(readFile(realDstDir + QLatin1String("/item.txt")), QByteArray("aliased"));
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
    bool sawFinalizing = false;
    for (const ProgressSnapshot& update : updates) {
        QCOMPARE(update.filesTotal, 1);
        QCOMPARE(update.bytesTotal, std::uint64_t(12));
        QVERIFY(update.filesDone >= prevFilesDone);
        QVERIFY(update.bytesDone >= prevBytesDone);
        if (update.phase == ProgressPhase::Finalizing) {
            sawFinalizing = true;
        }
        prevFilesDone = update.filesDone;
        prevBytesDone = update.bytesDone;
    }

    QVERIFY(sawFinalizing);
    QCOMPARE(result.finalProgress.filesTotal, 1);
    QCOMPARE(result.finalProgress.filesDone, 1);
    QCOMPARE(result.finalProgress.bytesTotal, std::uint64_t(12));
    QCOMPARE(result.finalProgress.bytesDone, std::uint64_t(12));
    QCOMPARE(result.finalProgress.phase, ProgressPhase::Finalizing);
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
    bool sawFinalizing = false;
    for (const ProgressSnapshot& update : updates) {
        QCOMPARE(update.filesTotal, result.finalProgress.filesTotal);
        QCOMPARE(update.bytesTotal, result.finalProgress.bytesTotal);
        QVERIFY(update.filesDone >= prevFilesDone);
        QVERIFY(update.bytesDone >= prevBytesDone);
        if (update.phase == ProgressPhase::Finalizing) {
            sawFinalizing = true;
        }
        prevFilesDone = update.filesDone;
        prevBytesDone = update.bytesDone;
    }

    QVERIFY(sawFinalizing);
    QCOMPARE(result.finalProgress.filesDone, result.finalProgress.filesTotal);
    QCOMPARE(result.finalProgress.bytesDone, result.finalProgress.bytesTotal);
    QCOMPARE(result.finalProgress.phase, ProgressPhase::Finalizing);
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

void FileOpsContractTest::sandboxedThreadCopyMatchesInProcessBehavior() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QLatin1String("/src");
    const QString dstInProcess = dir.path() + QLatin1String("/dst-inprocess");
    const QString dstSandboxed = dir.path() + QLatin1String("/dst-sandboxed");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstInProcess));
    QVERIFY(QDir().mkpath(dstSandboxed));

    const QString first = writeTempFile(srcDir + QLatin1String("/first.txt"), QByteArray("first-data"));
    const QString second = writeTempFile(srcDir + QLatin1String("/second.txt"), QByteArray("second-data"));
    const std::vector<std::string> sources = {toNative(first), toNative(second)};

    auto run_copy = [&](const QString& destination, WorkerMode worker_mode, QVector<ProgressSnapshot>& updates) {
        Request req;
        req.operation = Operation::Copy;
        req.sources = sources;
        req.destination.targetDir = toNative(destination);
        req.destination.mappingMode = DestinationMappingMode::SourceBasename;
        req.conflictPolicy = ConflictPolicy::Overwrite;
        req.linuxSafety.workerMode = worker_mode;

        EventHandlers handlers;
        handlers.onProgress = [&updates](const ProgressSnapshot& snapshot) { updates.push_back(snapshot); };
        return run(req, handlers);
    };

    QVector<ProgressSnapshot> in_process_updates;
    const Result in_process = run_copy(dstInProcess, WorkerMode::InProcess, in_process_updates);
    QVERIFY(in_process.success);
    QVERIFY(!in_process.cancelled);

    QVector<ProgressSnapshot> sandbox_updates;
    const Result sandboxed = run_copy(dstSandboxed, WorkerMode::SandboxedThread, sandbox_updates);
    QVERIFY(sandboxed.success);
    QVERIFY(!sandboxed.cancelled);

    QCOMPARE(readFile(dstInProcess + QLatin1String("/first.txt")),
             readFile(dstSandboxed + QLatin1String("/first.txt")));
    QCOMPARE(readFile(dstInProcess + QLatin1String("/second.txt")),
             readFile(dstSandboxed + QLatin1String("/second.txt")));

    QVERIFY(!in_process_updates.isEmpty());
    QVERIFY(!sandbox_updates.isEmpty());
    QCOMPARE(sandboxed.finalProgress.filesTotal, in_process.finalProgress.filesTotal);
    QCOMPARE(sandboxed.finalProgress.filesDone, in_process.finalProgress.filesDone);
    QCOMPARE(sandboxed.finalProgress.bytesTotal, in_process.finalProgress.bytesTotal);
    QCOMPARE(sandboxed.finalProgress.bytesDone, in_process.finalProgress.bytesDone);
    QCOMPARE(sandboxed.finalProgress.phase, in_process.finalProgress.phase);
}

void FileOpsContractTest::sandboxedThreadPromptConflictMatchesInProcessBehavior() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcInProcessDir = dir.path() + QLatin1String("/src-inprocess");
    const QString srcSandboxedDir = dir.path() + QLatin1String("/src-sandboxed");
    const QString dstInProcessDir = dir.path() + QLatin1String("/dst-inprocess");
    const QString dstSandboxedDir = dir.path() + QLatin1String("/dst-sandboxed");
    QVERIFY(QDir().mkpath(srcInProcessDir));
    QVERIFY(QDir().mkpath(srcSandboxedDir));
    QVERIFY(QDir().mkpath(dstInProcessDir));
    QVERIFY(QDir().mkpath(dstSandboxedDir));

    const QString inProcessSource =
        writeTempFile(srcInProcessDir + QLatin1String("/entry.txt"), QByteArray("updated-inprocess"));
    const QString sandboxedSource =
        writeTempFile(srcSandboxedDir + QLatin1String("/entry.txt"), QByteArray("updated-sandboxed"));
    const QString inProcessExisting =
        writeTempFile(dstInProcessDir + QLatin1String("/entry.txt"), QByteArray("existing-inprocess"));
    const QString sandboxedExisting =
        writeTempFile(dstSandboxedDir + QLatin1String("/entry.txt"), QByteArray("existing-sandboxed"));

    auto run_prompt = [&](const QString& source, const QString& destination, WorkerMode worker_mode,
                          int& conflict_count) {
        Request req;
        req.operation = Operation::Copy;
        req.sources = {toNative(source)};
        req.destination.targetDir = toNative(destination);
        req.destination.mappingMode = DestinationMappingMode::SourceBasename;
        req.conflictPolicy = ConflictPolicy::Prompt;
        req.linuxSafety.workerMode = worker_mode;

        EventHandlers handlers;
        handlers.onConflict = [&conflict_count](const ConflictEvent&) -> ConflictResolution {
            ++conflict_count;
            return ConflictResolution::Rename;
        };
        return run(req, handlers);
    };

    int in_process_conflicts = 0;
    const Result in_process = run_prompt(inProcessSource, dstInProcessDir, WorkerMode::InProcess, in_process_conflicts);
    QVERIFY(in_process.success);
    QVERIFY(!in_process.cancelled);
    QCOMPARE(in_process_conflicts, 1);
    QCOMPARE(readFile(inProcessExisting), QByteArray("existing-inprocess"));
    QCOMPARE(readFile(dstInProcessDir + QLatin1String("/entry (copy).txt")), QByteArray("updated-inprocess"));

    int sandboxed_conflicts = 0;
    const Result sandboxed =
        run_prompt(sandboxedSource, dstSandboxedDir, WorkerMode::SandboxedThread, sandboxed_conflicts);
    QVERIFY(sandboxed.success);
    QVERIFY(!sandboxed.cancelled);
    QCOMPARE(sandboxed_conflicts, 1);
    QCOMPARE(readFile(sandboxedExisting), QByteArray("existing-sandboxed"));
    QCOMPARE(readFile(dstSandboxedDir + QLatin1String("/entry (copy).txt")), QByteArray("updated-sandboxed"));

    QCOMPARE(sandboxed.finalProgress.filesTotal, in_process.finalProgress.filesTotal);
    QCOMPARE(sandboxed.finalProgress.filesDone, in_process.finalProgress.filesDone);
    QCOMPARE(sandboxed.finalProgress.bytesTotal, in_process.finalProgress.bytesTotal);
    QCOMPARE(sandboxed.finalProgress.bytesDone, in_process.finalProgress.bytesDone);
}

void FileOpsContractTest::sandboxedThreadCancelReturnsEcanceled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = dir.path() + QLatin1String("/large.bin");
    QVERIFY(createSparseFile(srcPath, qint64(2) * 1024 * 1024 * 1024));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/large.bin");

    std::atomic<bool> cancelRequested{false};

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(srcPath)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.conflictPolicy = ConflictPolicy::Overwrite;
    req.linuxSafety.workerMode = WorkerMode::SandboxedThread;
    req.cancellationRequested = [&cancelRequested]() { return cancelRequested.load(); };

    EventHandlers handlers;
    handlers.onProgress = [&cancelRequested](const ProgressSnapshot& snapshot) {
        if (snapshot.bytesDone > 0) {
            cancelRequested.store(true);
        }
    };

    const Result result = run(req, handlers);
    QVERIFY(!result.success);
    QVERIFY(result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::Cancelled);
    QCOMPARE(result.error.sysErrno, ECANCELED);
    QCOMPARE(result.finalProgress.filesTotal, 1);
    QVERIFY(result.finalProgress.bytesTotal > 0);
    QVERIFY(result.finalProgress.bytesDone > 0);
    QVERIFY(result.finalProgress.bytesDone < result.finalProgress.bytesTotal);
    QVERIFY(!result.finalProgress.currentPath.empty());
    QVERIFY(!QFileInfo::exists(dstPath));
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

void FileOpsContractTest::capabilityReportExposesLocalAndGioBackends() {
    const CapabilityReport report = capabilities();

    QCOMPARE(report.localHardened.backend, Backend::LocalHardened);
    QVERIFY(report.localHardened.available);
    QVERIFY(report.localHardened.supportsNativePaths);
    QVERIFY(!report.localHardened.supportsUriPaths);
    QVERIFY(report.localHardened.supportsCopy);
    QVERIFY(report.localHardened.supportsMove);
    QVERIFY(report.localHardened.supportsDelete);
    QVERIFY(!report.localHardened.supportsTrash);
    QVERIFY(!report.localHardened.supportsUntrash);

    QCOMPARE(report.gio.backend, Backend::Gio);
    QVERIFY(report.gio.available);
    QVERIFY(report.gio.supportsUriPaths);
    QVERIFY(report.gio.supportsNativePaths);
    QVERIFY(report.gio.supportsCopy);
    QVERIFY(report.gio.supportsMove);
    QVERIFY(report.gio.supportsDelete);
    QVERIFY(report.gio.supportsTrash);
    QVERIFY(report.gio.supportsUntrash);
    QVERIFY(report.gio.unavailableReason.empty());
}

void FileOpsContractTest::routingSourceKindSizeMismatchRejected() {
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
    req.routing.sourceKinds = {EndpointKind::NativePath, EndpointKind::NativePath};

    const Result result = preflight(req);
    QVERIFY(!result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::InvalidRequest);
    QCOMPARE(result.error.sysErrno, EINVAL);
    QVERIFY(QString::fromLocal8Bit(result.error.message.c_str()).contains(QStringLiteral("sourceKinds")));
}

void FileOpsContractTest::uriSourceCanRunViaGioBackend() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = writeTempFile(dir.path() + QLatin1String("/src.txt"), QByteArray("gio-copy"));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));
    const QString dstPath = dstDir + QLatin1String("/src.txt");

    Request req;
    req.operation = Operation::Copy;
    req.sources = {QUrl::fromLocalFile(srcPath).toString().toStdString()};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.routing.defaultBackend = Backend::Gio;
    req.routing.sourceKinds = {EndpointKind::Uri};
    req.routing.destinationKind = EndpointKind::NativePath;

    const Result result = run(req);
    QVERIFY(result.success);
    QVERIFY(!result.cancelled);
    QVERIFY(result.error.code == EngineErrorCode::None);
    QCOMPARE(readFile(dstPath), QByteArray("gio-copy"));
    QCOMPARE(result.finalProgress.filesTotal, 1);
    QCOMPARE(result.finalProgress.filesDone, 1);
}

void FileOpsContractTest::forcingLocalBackendForUriIsRejected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));

    Request req;
    req.operation = Operation::Copy;
    req.sources = {"sftp://example.invalid/path/to/file.txt"};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.routing.sourceKinds = {EndpointKind::Uri};
    req.routing.destinationKind = EndpointKind::NativePath;
    req.routing.defaultBackend = Backend::LocalHardened;

    const Result result = preflight(req);
    QVERIFY(!result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::UnsupportedPolicy);
    QCOMPARE(result.error.sysErrno, ENOTSUP);
    QVERIFY(QString::fromLocal8Bit(result.error.message.c_str()).contains(QStringLiteral("LocalHardened")));
}

void FileOpsContractTest::trashAndUntrashPreflightOnGioBackend() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir.path() + QLatin1String("/src.txt"), QByteArray("x"));

    Request trashReq;
    trashReq.operation = Operation::Trash;
    trashReq.sources = {toNative(src)};
    trashReq.routing.defaultBackend = Backend::Gio;
    trashReq.routing.sourceBackends = {Backend::Gio};

    const Result trashResult = preflight(trashReq);
    QVERIFY(trashResult.success);
    QVERIFY(!trashResult.cancelled);
    QCOMPARE(trashResult.error.code, EngineErrorCode::None);

    Request untrashReq;
    untrashReq.operation = Operation::Untrash;
    untrashReq.sources = {"trash:///placeholder-entry"};
    untrashReq.routing.defaultBackend = Backend::Gio;
    untrashReq.routing.sourceKinds = {EndpointKind::Uri};
    untrashReq.routing.sourceBackends = {Backend::Gio};

    const Result untrashResult = preflight(untrashReq);
    QVERIFY(untrashResult.success);
    QVERIFY(!untrashResult.cancelled);
    QCOMPARE(untrashResult.error.code, EngineErrorCode::None);
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

void FileOpsContractTest::requireOpenat2ResolveFailsFastWhenUnavailable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString src = writeTempFile(dir.path() + QLatin1String("/src.txt"), QByteArray("x"));
    const QString dstDir = dir.path() + QLatin1String("/dst");
    QVERIFY(QDir().mkpath(dstDir));

    ScopedEnvVar forceOpenat2Missing("PCMANFM_TEST_FORCE_OPENAT2_ENOSYS", "1");

    Request req;
    req.operation = Operation::Copy;
    req.sources = {toNative(src)};
    req.destination.targetDir = toNative(dstDir);
    req.destination.mappingMode = DestinationMappingMode::SourceBasename;
    req.linuxSafety.requireOpenat2Resolve = true;

    const Result result = run(req);
    QVERIFY(!result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::SafetyRequirementUnavailable);
    QCOMPARE(result.error.sysErrno, ENOSYS);
    QVERIFY(QString::fromLocal8Bit(result.error.message.c_str()).contains(QStringLiteral("openat2")));
}

void FileOpsContractTest::requireSeccompNeedsSandboxedWorkerMode() {
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
    req.linuxSafety.requireSeccomp = true;

    const Result result = run(req);
    QVERIFY(!result.success);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.error.code, EngineErrorCode::UnsupportedPolicy);
    QCOMPARE(result.error.sysErrno, ENOTSUP);
}

QTEST_MAIN(FileOpsContractTest)
#include "file_ops_contract_test.moc"
