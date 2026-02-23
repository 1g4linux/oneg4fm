/*
 * Cross-layer parity tests for core, Qt adapter, and libfm-qt bridge
 * tests/cross_layer_parity_test.cpp
 */

#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "../src/core/file_ops_contract.h"
#include "../src/backends/qt/qt_fileops.h"

#include "../libfm-qt/src/core/filetransferjob.h"

#include <cstdint>
#include <cerrno>

namespace {

enum class ConflictAction {
    Skip,
    Cancel,
};

struct ProgressSnapshot {
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    int filesDone = 0;
    int filesTotal = 0;
};

struct LayerOutcome {
    bool success = false;
    bool cancelled = false;
    int conflictCount = 0;
    int sysErrno = 0;
    QString errorMessage;
    ProgressSnapshot progress;
};

struct ScenarioPaths {
    QString srcPath;
    QString dstDir;
    QString dstPath;
};

std::string toNative(const QString& path) {
    const QByteArray encoded = QFile::encodeName(path);
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

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

ScenarioPaths makeScenario(const QTemporaryDir& root,
                           const QString& layerName,
                           const QByteArray& sourceData,
                           const QByteArray& destinationData) {
    const QString base = root.path() + QLatin1Char('/') + layerName;
    const QString srcDir = base + QLatin1String("/src");
    const QString dstDir = base + QLatin1String("/dst");
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir);

    ScenarioPaths out;
    out.srcPath = writeFile(srcDir + QLatin1String("/item.txt"), sourceData);
    out.dstDir = dstDir;
    out.dstPath = writeFile(dstDir + QLatin1String("/item.txt"), destinationData);
    return out;
}

LayerOutcome runCoreCopyWithPromptConflict(const ScenarioPaths& paths, ConflictAction action) {
    LayerOutcome out;

    Oneg4FM::FileOpsContract::Request req;
    req.operation = Oneg4FM::FileOpsContract::Operation::Copy;
    req.sources = {toNative(paths.srcPath)};
    req.destination.targetDir = toNative(paths.dstDir);
    req.destination.mappingMode = Oneg4FM::FileOpsContract::DestinationMappingMode::SourceBasename;
    req.conflictPolicy = Oneg4FM::FileOpsContract::ConflictPolicy::Prompt;
    req.linuxSafety.workerMode = Oneg4FM::FileOpsContract::WorkerMode::InProcess;

    Oneg4FM::FileOpsContract::EventHandlers handlers;
    handlers.onConflict = [&out, action](const Oneg4FM::FileOpsContract::ConflictEvent&) {
        ++out.conflictCount;
        return action == ConflictAction::Skip ? Oneg4FM::FileOpsContract::ConflictResolution::Skip
                                              : Oneg4FM::FileOpsContract::ConflictResolution::Abort;
    };

    const Oneg4FM::FileOpsContract::Result result = Oneg4FM::FileOpsContract::run(req, handlers);
    out.success = result.success;
    out.cancelled = result.cancelled;
    out.sysErrno = result.error.sysErrno;
    out.errorMessage = QString::fromLocal8Bit(result.error.message.c_str());
    out.progress.bytesDone = result.finalProgress.bytesDone;
    out.progress.bytesTotal = result.finalProgress.bytesTotal;
    out.progress.filesDone = result.finalProgress.filesDone;
    out.progress.filesTotal = result.finalProgress.filesTotal;
    return out;
}

LayerOutcome runQtCopyWithPromptConflict(const ScenarioPaths& paths, ConflictAction action) {
    LayerOutcome out;

    Oneg4FM::QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &Oneg4FM::QtFileOps::finished);
    QObject::connect(&ops, &Oneg4FM::QtFileOps::progress, &ops, [&out](const Oneg4FM::FileOpProgress& info) {
        out.progress.bytesDone = info.bytesDone;
        out.progress.bytesTotal = info.bytesTotal;
        out.progress.filesDone = info.filesDone;
        out.progress.filesTotal = info.filesTotal;
    });
    QObject::connect(&ops, &Oneg4FM::QtFileOps::conflictRequested, &ops,
                     [&ops, &out, action](const Oneg4FM::FileOpConflict&) {
                         ++out.conflictCount;
                         ops.resolveConflict(action == ConflictAction::Skip ? Oneg4FM::FileOpConflictResolution::Skip
                                                                            : Oneg4FM::FileOpConflictResolution::Abort);
                     });

    Oneg4FM::FileOpRequest req;
    req.type = Oneg4FM::FileOpType::Copy;
    req.sources = QStringList{paths.srcPath};
    req.destination = paths.dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.promptOnConflict = true;

    ops.start(req);
    for (int i = 0; i < 300 && finishedSpy.count() == 0; ++i) {
        QTest::qWait(10);
    }
    if (finishedSpy.count() == 0) {
        out.success = false;
        out.cancelled = false;
        out.sysErrno = ETIMEDOUT;
        out.errorMessage = QStringLiteral("Timed out waiting for Qt adapter completion");
        return out;
    }

    const QList<QVariant> args = finishedSpy.takeFirst();
    out.success = args.at(0).toBool();
    out.errorMessage = args.at(1).toString();
    out.cancelled = !out.success && out.errorMessage.contains(QStringLiteral("cancel"), Qt::CaseInsensitive);
    out.sysErrno = out.cancelled ? ECANCELED : 0;
    return out;
}

LayerOutcome runLibfmCopyWithPromptConflict(const ScenarioPaths& paths, ConflictAction action) {
    LayerOutcome out;

    Fm::FilePathList sources;
    sources.emplace_back(toLocalFilePath(paths.srcPath));

    Fm::FileTransferJob job(std::move(sources), Fm::FileTransferJob::Mode::COPY);
    job.setDestDirPath(toLocalFilePath(paths.dstDir));

    QObject::connect(
        &job, &Fm::FileOperationJob::fileExists, &job,
        [&out, action](const Fm::FileInfo&, const Fm::FileInfo&, Fm::FileOperationJob::FileExistsAction& response,
                       Fm::FilePath&) {
            ++out.conflictCount;
            response = action == ConflictAction::Skip ? Fm::FileOperationJob::SKIP : Fm::FileOperationJob::CANCEL;
        },
        Qt::DirectConnection);

    job.run();

    out.cancelled = job.isCancelled();
    out.success = !out.cancelled;
    out.sysErrno = out.cancelled ? ECANCELED : 0;

    std::uint64_t totalBytes = 0;
    std::uint64_t totalFiles = 0;
    std::uint64_t finishedBytes = 0;
    std::uint64_t finishedFiles = 0;
    if (job.totalAmount(totalBytes, totalFiles) && job.finishedAmount(finishedBytes, finishedFiles)) {
        out.progress.bytesTotal = totalBytes;
        out.progress.filesTotal = static_cast<int>(totalFiles);
        out.progress.bytesDone = finishedBytes;
        out.progress.filesDone = static_cast<int>(finishedFiles);
    }

    return out;
}

}  // namespace

class CrossLayerParityTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void copyPromptSkipParityAcrossCoreQtAndLibfmQt();
    void copyPromptCancelParityAcrossCoreQtAndLibfmQt();
};

void CrossLayerParityTest::copyPromptSkipParityAcrossCoreQtAndLibfmQt() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray sourceData("new-data");
    const QByteArray destinationData("existing-data");

    const ScenarioPaths corePaths = makeScenario(dir, QStringLiteral("core"), sourceData, destinationData);
    const ScenarioPaths qtPaths = makeScenario(dir, QStringLiteral("qt"), sourceData, destinationData);
    const ScenarioPaths libfmPaths = makeScenario(dir, QStringLiteral("libfm"), sourceData, destinationData);

    const LayerOutcome core = runCoreCopyWithPromptConflict(corePaths, ConflictAction::Skip);
    const LayerOutcome qt = runQtCopyWithPromptConflict(qtPaths, ConflictAction::Skip);
    const LayerOutcome libfm = runLibfmCopyWithPromptConflict(libfmPaths, ConflictAction::Skip);

    QVERIFY(core.success);
    QVERIFY(qt.success);
    QVERIFY(libfm.success);
    QVERIFY(!core.cancelled);
    QVERIFY(!qt.cancelled);
    QVERIFY(!libfm.cancelled);

    QCOMPARE(core.conflictCount, 1);
    QCOMPARE(qt.conflictCount, 1);
    QCOMPARE(libfm.conflictCount, 1);

    QCOMPARE(readFile(corePaths.dstPath), destinationData);
    QCOMPARE(readFile(qtPaths.dstPath), destinationData);
    QCOMPARE(readFile(libfmPaths.dstPath), destinationData);

    QCOMPARE(core.sysErrno, 0);
    QCOMPARE(qt.sysErrno, 0);
    QCOMPARE(libfm.sysErrno, 0);
    QVERIFY(qt.errorMessage.isEmpty());

    QCOMPARE(core.progress.filesTotal, 1);
    QCOMPARE(qt.progress.filesTotal, 1);
    QCOMPARE(libfm.progress.filesTotal, 1);
    QCOMPARE(core.progress.filesDone, 1);
    QCOMPARE(qt.progress.filesDone, 1);
    QCOMPARE(libfm.progress.filesDone, 1);

    QCOMPARE(core.progress.bytesTotal, static_cast<std::uint64_t>(sourceData.size()));
    QCOMPARE(qt.progress.bytesTotal, static_cast<std::uint64_t>(sourceData.size()));
    QCOMPARE(libfm.progress.bytesTotal, static_cast<std::uint64_t>(sourceData.size()));
    QCOMPARE(core.progress.bytesDone, core.progress.bytesTotal);
    QCOMPARE(qt.progress.bytesDone, qt.progress.bytesTotal);
    QCOMPARE(libfm.progress.bytesDone, libfm.progress.bytesTotal);

    QCOMPARE(core.progress.bytesDone, qt.progress.bytesDone);
    QCOMPARE(core.progress.bytesDone, libfm.progress.bytesDone);
}

void CrossLayerParityTest::copyPromptCancelParityAcrossCoreQtAndLibfmQt() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray sourceData("new-data");
    const QByteArray destinationData("existing-data");

    const ScenarioPaths corePaths = makeScenario(dir, QStringLiteral("core-cancel"), sourceData, destinationData);
    const ScenarioPaths qtPaths = makeScenario(dir, QStringLiteral("qt-cancel"), sourceData, destinationData);
    const ScenarioPaths libfmPaths = makeScenario(dir, QStringLiteral("libfm-cancel"), sourceData, destinationData);

    const LayerOutcome core = runCoreCopyWithPromptConflict(corePaths, ConflictAction::Cancel);
    const LayerOutcome qt = runQtCopyWithPromptConflict(qtPaths, ConflictAction::Cancel);
    const LayerOutcome libfm = runLibfmCopyWithPromptConflict(libfmPaths, ConflictAction::Cancel);

    QVERIFY(!core.success);
    QVERIFY(!qt.success);
    QVERIFY(!libfm.success);
    QVERIFY(core.cancelled);
    QVERIFY(qt.cancelled);
    QVERIFY(libfm.cancelled);

    QCOMPARE(core.conflictCount, 1);
    QCOMPARE(qt.conflictCount, 1);
    QCOMPARE(libfm.conflictCount, 1);

    QCOMPARE(core.sysErrno, ECANCELED);
    QCOMPARE(qt.sysErrno, ECANCELED);
    QCOMPARE(libfm.sysErrno, ECANCELED);
    QVERIFY(qt.errorMessage.contains(QStringLiteral("cancel"), Qt::CaseInsensitive));

    QCOMPARE(readFile(corePaths.dstPath), destinationData);
    QCOMPARE(readFile(qtPaths.dstPath), destinationData);
    QCOMPARE(readFile(libfmPaths.dstPath), destinationData);

    QCOMPARE(core.progress.filesTotal, 1);
    QCOMPARE(libfm.progress.filesTotal, 1);
    QCOMPARE(core.progress.filesDone, 0);
    QCOMPARE(libfm.progress.filesDone, 0);
    QCOMPARE(core.progress.bytesDone, std::uint64_t(0));
    QCOMPARE(libfm.progress.bytesDone, std::uint64_t(0));
}

QTEST_MAIN(CrossLayerParityTest)
#include "cross_layer_parity_test.moc"
