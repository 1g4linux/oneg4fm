/*
 * Cross-layer parity tests for core, Qt adapter, and libfm-qt bridge
 * tests/cross_layer_parity_test.cpp
 */

#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QVector>

#include "../src/core/file_ops_contract.h"
#include "../src/backends/qt/qt_fileops.h"

#include "../libfm-qt/src/core/deletejob.h"
#include "../libfm-qt/src/core/filetransferjob.h"
#include "../libfm-qt/src/core/trashjob.h"
#include "../libfm-qt/src/core/untrashjob.h"

#include <gio/gio.h>

#include <cerrno>
#include <cstdint>
#include <string>

namespace {

namespace CoreFileOps = Oneg4FM::FileOpsContract;

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
    int promptCount = 0;
    int conflictCount = 0;
    int sysErrno = 0;
    QString errorMessage;
    ProgressSnapshot progress;
    QVector<ProgressSnapshot> progressSamples;
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

std::string toUtf8(const QString& text) {
    const QByteArray encoded = text.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

Fm::FilePath toLocalFilePath(const QString& path) {
    const QByteArray encoded = QFile::encodeName(path);
    return Fm::FilePath::fromLocalPath(encoded.constData());
}

Fm::FilePath toUriFilePath(const QString& uri) {
    const QByteArray encoded = uri.toUtf8();
    return Fm::FilePath::fromUri(encoded.constData());
}

QString writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(data);
        file.close();
    }
    return path;
}

QString writeLayerFile(const QString& layerDir, const QString& fileName, const QByteArray& data) {
    QDir().mkpath(layerDir);
    return writeFile(layerDir + QLatin1Char('/') + fileName, data);
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

ProgressSnapshot toProgressSnapshot(const CoreFileOps::ProgressSnapshot& progress) {
    ProgressSnapshot out;
    out.bytesDone = progress.bytesDone;
    out.bytesTotal = progress.bytesTotal;
    out.filesDone = progress.filesDone;
    out.filesTotal = progress.filesTotal;
    return out;
}

ProgressSnapshot toProgressSnapshot(const Oneg4FM::FileOpProgress& progress) {
    ProgressSnapshot out;
    out.bytesDone = progress.bytesDone;
    out.bytesTotal = progress.bytesTotal;
    out.filesDone = progress.filesDone;
    out.filesTotal = progress.filesTotal;
    return out;
}

bool sameProgress(const ProgressSnapshot& lhs, const ProgressSnapshot& rhs) {
    return lhs.bytesDone == rhs.bytesDone && lhs.bytesTotal == rhs.bytesTotal && lhs.filesDone == rhs.filesDone &&
           lhs.filesTotal == rhs.filesTotal;
}

void appendProgress(LayerOutcome& out, const ProgressSnapshot& progress) {
    out.progress = progress;
    out.progressSamples.push_back(progress);
}

void ensureProgressSample(LayerOutcome& out) {
    if (out.progressSamples.isEmpty()) {
        out.progressSamples.push_back(out.progress);
    }
}

void finalizeCoreOutcome(const CoreFileOps::Result& result, LayerOutcome& out) {
    out.success = result.success;
    out.cancelled = result.cancelled;
    out.sysErrno = result.error.sysErrno;
    out.errorMessage = QString::fromLocal8Bit(result.error.message.c_str());

    const ProgressSnapshot finalProgress = toProgressSnapshot(result.finalProgress);
    if (out.progressSamples.isEmpty() || !sameProgress(out.progressSamples.back(), finalProgress)) {
        appendProgress(out, finalProgress);
    }
    else {
        out.progress = finalProgress;
    }
}

void captureLibfmProgress(Fm::FileOperationJob& job, LayerOutcome& out) {
    std::uint64_t totalBytes = 0;
    std::uint64_t totalFiles = 0;
    std::uint64_t finishedBytes = 0;
    std::uint64_t finishedFiles = 0;
    if (job.totalAmount(totalBytes, totalFiles) && job.finishedAmount(finishedBytes, finishedFiles)) {
        ProgressSnapshot progress;
        progress.bytesTotal = totalBytes;
        progress.filesTotal = static_cast<int>(totalFiles);
        progress.bytesDone = finishedBytes;
        progress.filesDone = static_cast<int>(finishedFiles);
        appendProgress(out, progress);
    }
}

bool isProgressMonotonic(const QVector<ProgressSnapshot>& samples) {
    if (samples.isEmpty()) {
        return true;
    }

    for (int i = 1; i < samples.size(); ++i) {
        const ProgressSnapshot& prev = samples.at(i - 1);
        const ProgressSnapshot& current = samples.at(i);
        if (current.bytesDone < prev.bytesDone) {
            return false;
        }
        if (current.bytesTotal < prev.bytesTotal) {
            return false;
        }
        if (current.filesDone < prev.filesDone) {
            return false;
        }
        if (current.filesTotal < prev.filesTotal) {
            return false;
        }
        if (current.bytesTotal > 0 && current.bytesDone > current.bytesTotal) {
            return false;
        }
        if (current.filesTotal > 0 && current.filesDone > current.filesTotal) {
            return false;
        }
    }

    return true;
}

void verifySuccessCompletion(const LayerOutcome& outcome) {
    QVERIFY(outcome.success);
    QVERIFY(!outcome.cancelled);
    QVERIFY2(isProgressMonotonic(outcome.progressSamples), "Progress counters must be monotonic");
    if (outcome.progress.filesTotal > 0) {
        QCOMPARE(outcome.progress.filesDone, outcome.progress.filesTotal);
    }
    if (outcome.progress.bytesTotal > 0) {
        QCOMPARE(outcome.progress.bytesDone, outcome.progress.bytesTotal);
    }
}

QString uniqueToken() {
    QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    token.remove(QLatin1Char('-'));
    return token;
}

bool listTrashUris(QSet<QString>& urisOut, QString& errorOut) {
    urisOut.clear();
    errorOut.clear();

    GFile* const trashRoot = g_file_new_for_uri("trash:///");
    if (!trashRoot) {
        errorOut = QStringLiteral("Unable to allocate trash root GFile");
        return false;
    }

    GError* enumerateError = nullptr;
    GFileEnumerator* const enumerator = g_file_enumerate_children(trashRoot, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                                  G_FILE_QUERY_INFO_NONE, nullptr, &enumerateError);
    if (!enumerator) {
        errorOut = enumerateError ? QString::fromUtf8(enumerateError->message)
                                  : QStringLiteral("Unable to enumerate trash:/// entries");
        if (enumerateError) {
            g_error_free(enumerateError);
        }
        g_object_unref(trashRoot);
        return false;
    }

    bool ok = true;
    while (true) {
        GError* nextError = nullptr;
        GFileInfo* const info = g_file_enumerator_next_file(enumerator, nullptr, &nextError);
        if (!info) {
            if (nextError) {
                errorOut = QString::fromUtf8(nextError->message);
                g_error_free(nextError);
                ok = false;
            }
            break;
        }

        const char* const name = g_file_info_get_name(info);
        if (name && name[0] != '\0') {
            GFile* const child = g_file_get_child(trashRoot, name);
            if (child) {
                char* const uri = g_file_get_uri(child);
                if (uri) {
                    urisOut.insert(QString::fromUtf8(uri));
                    g_free(uri);
                }
                g_object_unref(child);
            }
        }
        g_object_unref(info);
    }

    g_file_enumerator_close(enumerator, nullptr, nullptr);
    g_object_unref(enumerator);
    g_object_unref(trashRoot);
    return ok;
}

QString findAddedTrashUri(const QSet<QString>& before, const QSet<QString>& after, const QString& token) {
    QStringList matches;
    for (const QString& uri : after) {
        if (before.contains(uri)) {
            continue;
        }
        if (uri.contains(token, Qt::CaseInsensitive)) {
            matches.push_back(uri);
        }
    }

    return matches.size() == 1 ? matches.front() : QString();
}

LayerOutcome runQtRequest(const Oneg4FM::FileOpRequest& req, ConflictAction action) {
    LayerOutcome out;

    Oneg4FM::QtFileOps ops;
    QSignalSpy finishedSpy(&ops, &Oneg4FM::QtFileOps::finished);

    QObject::connect(&ops, &Oneg4FM::QtFileOps::progress, &ops,
                     [&out](const Oneg4FM::FileOpProgress& info) { appendProgress(out, toProgressSnapshot(info)); });

    QObject::connect(
        &ops, &Oneg4FM::QtFileOps::conflictRequested, &ops,
        [&ops, &out, action](const Oneg4FM::FileOpConflict&) {
            ++out.promptCount;
            ++out.conflictCount;
            const Oneg4FM::FileOpConflictResolution resolution = action == ConflictAction::Skip
                                                                     ? Oneg4FM::FileOpConflictResolution::Skip
                                                                     : Oneg4FM::FileOpConflictResolution::Abort;
            ops.resolveConflict(resolution);
        },
        Qt::DirectConnection);

    ops.start(req);
    for (int i = 0; i < 300 && finishedSpy.count() == 0; ++i) {
        QTest::qWait(10);
    }
    if (finishedSpy.count() == 0) {
        out.success = false;
        out.cancelled = false;
        out.sysErrno = ETIMEDOUT;
        out.errorMessage = QStringLiteral("Timed out waiting for Qt adapter completion");
        ensureProgressSample(out);
        return out;
    }

    const QList<QVariant> args = finishedSpy.takeFirst();
    out.success = args.at(0).toBool();
    out.errorMessage = args.at(1).toString();
    out.cancelled = !out.success && out.errorMessage.contains(QStringLiteral("cancel"), Qt::CaseInsensitive);
    out.sysErrno = out.cancelled ? ECANCELED : 0;
    ensureProgressSample(out);
    return out;
}

LayerOutcome runCoreCopyWithPromptConflict(const ScenarioPaths& paths, ConflictAction action) {
    LayerOutcome out;

    CoreFileOps::Request req;
    req.operation = CoreFileOps::Operation::Copy;
    req.sources = {toNative(paths.srcPath)};
    req.destination.targetDir = toNative(paths.dstDir);
    req.destination.mappingMode = CoreFileOps::DestinationMappingMode::SourceBasename;
    req.conflictPolicy = CoreFileOps::ConflictPolicy::Prompt;
    req.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;

    CoreFileOps::EventHandlers handlers;
    handlers.onProgress = [&out](const CoreFileOps::ProgressSnapshot& info) {
        appendProgress(out, toProgressSnapshot(info));
    };
    handlers.onConflict = [&out, action](const CoreFileOps::ConflictEvent&) {
        ++out.conflictCount;
        return action == ConflictAction::Skip ? CoreFileOps::ConflictResolution::Skip
                                              : CoreFileOps::ConflictResolution::Abort;
    };

    CoreFileOps::EventStreamHandlers streamHandlers;
    streamHandlers.onPrompt = [&out](const CoreFileOps::PromptEvent& event) {
        if (event.kind == CoreFileOps::PromptKind::ConflictResolution) {
            ++out.promptCount;
        }
    };

    const CoreFileOps::Result result = CoreFileOps::run(req, handlers, streamHandlers);
    finalizeCoreOutcome(result, out);
    return out;
}

LayerOutcome runQtCopyWithPromptConflict(const ScenarioPaths& paths, ConflictAction action) {
    Oneg4FM::FileOpRequest req;
    req.type = Oneg4FM::FileOpType::Copy;
    req.sources = QStringList{paths.srcPath};
    req.destination = paths.dstDir;
    req.followSymlinks = false;
    req.overwriteExisting = false;
    req.promptOnConflict = true;
    return runQtRequest(req, action);
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
            ++out.promptCount;
            ++out.conflictCount;
            response = action == ConflictAction::Skip ? Fm::FileOperationJob::SKIP : Fm::FileOperationJob::CANCEL;
        },
        Qt::DirectConnection);

    job.run();

    out.cancelled = job.isCancelled();
    out.success = !out.cancelled;
    out.sysErrno = out.cancelled ? ECANCELED : 0;
    captureLibfmProgress(job, out);
    ensureProgressSample(out);
    return out;
}

LayerOutcome runCoreDelete(const QString& sourcePath) {
    LayerOutcome out;

    CoreFileOps::Request req;
    req.operation = CoreFileOps::Operation::Delete;
    req.sources = {toNative(sourcePath)};
    req.routing.defaultBackend = CoreFileOps::Backend::LocalHardened;
    req.routing.sourceKinds = {CoreFileOps::EndpointKind::NativePath};
    req.routing.sourceBackends = {CoreFileOps::Backend::LocalHardened};
    req.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;

    CoreFileOps::EventHandlers handlers;
    handlers.onProgress = [&out](const CoreFileOps::ProgressSnapshot& info) {
        appendProgress(out, toProgressSnapshot(info));
    };

    const CoreFileOps::Result result = CoreFileOps::run(req, handlers);
    finalizeCoreOutcome(result, out);
    return out;
}

LayerOutcome runQtDelete(const QString& sourcePath) {
    Oneg4FM::FileOpRequest req;
    req.type = Oneg4FM::FileOpType::Delete;
    req.sources = QStringList{sourcePath};
    return runQtRequest(req, ConflictAction::Skip);
}

LayerOutcome runLibfmDelete(const QString& sourcePath) {
    LayerOutcome out;

    Fm::FilePathList targets;
    targets.emplace_back(toLocalFilePath(sourcePath));

    Fm::DeleteJob job(std::move(targets));
    job.run();

    out.cancelled = job.isCancelled();
    out.success = !out.cancelled;
    out.sysErrno = out.cancelled ? ECANCELED : 0;
    captureLibfmProgress(job, out);
    ensureProgressSample(out);
    return out;
}

LayerOutcome runCoreTrash(const QString& sourcePath) {
    LayerOutcome out;

    CoreFileOps::Request req;
    req.operation = CoreFileOps::Operation::Trash;
    req.sources = {toNative(sourcePath)};
    req.routing.defaultBackend = CoreFileOps::Backend::Gio;
    req.routing.sourceKinds = {CoreFileOps::EndpointKind::NativePath};
    req.routing.sourceBackends = {CoreFileOps::Backend::Gio};
    req.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;

    CoreFileOps::EventHandlers handlers;
    handlers.onProgress = [&out](const CoreFileOps::ProgressSnapshot& info) {
        appendProgress(out, toProgressSnapshot(info));
    };

    const CoreFileOps::Result result = CoreFileOps::run(req, handlers);
    finalizeCoreOutcome(result, out);
    return out;
}

LayerOutcome runLibfmTrash(const QString& sourcePath) {
    LayerOutcome out;

    Fm::FilePathList paths;
    paths.emplace_back(toLocalFilePath(sourcePath));

    Fm::TrashJob job(std::move(paths));
    job.run();

    const bool hasUnsupported = !job.unsupportedFiles().empty();
    out.cancelled = job.isCancelled();
    out.success = !out.cancelled && !hasUnsupported;
    out.sysErrno = out.cancelled ? ECANCELED : (hasUnsupported ? ENOTSUP : 0);
    if (hasUnsupported) {
        out.errorMessage = QStringLiteral("Trash operation unsupported for one or more paths");
    }
    captureLibfmProgress(job, out);
    ensureProgressSample(out);
    return out;
}

LayerOutcome runCoreUntrash(const QString& trashUri) {
    LayerOutcome out;

    CoreFileOps::Request req;
    req.operation = CoreFileOps::Operation::Untrash;
    req.sources = {toUtf8(trashUri)};
    req.conflictPolicy = CoreFileOps::ConflictPolicy::Prompt;
    req.routing.defaultBackend = CoreFileOps::Backend::Gio;
    req.routing.sourceKinds = {CoreFileOps::EndpointKind::Uri};
    req.routing.sourceBackends = {CoreFileOps::Backend::Gio};
    req.linuxSafety.workerMode = CoreFileOps::WorkerMode::InProcess;

    CoreFileOps::EventHandlers handlers;
    handlers.onProgress = [&out](const CoreFileOps::ProgressSnapshot& info) {
        appendProgress(out, toProgressSnapshot(info));
    };
    handlers.onConflict = [&out](const CoreFileOps::ConflictEvent&) {
        ++out.conflictCount;
        return CoreFileOps::ConflictResolution::Skip;
    };

    CoreFileOps::EventStreamHandlers streamHandlers;
    streamHandlers.onPrompt = [&out](const CoreFileOps::PromptEvent& event) {
        if (event.kind == CoreFileOps::PromptKind::ConflictResolution) {
            ++out.promptCount;
        }
    };

    const CoreFileOps::Result result = CoreFileOps::run(req, handlers, streamHandlers);
    finalizeCoreOutcome(result, out);
    return out;
}

LayerOutcome runLibfmUntrash(const QString& trashUri) {
    LayerOutcome out;

    Fm::FilePathList paths;
    paths.emplace_back(toUriFilePath(trashUri));

    Fm::UntrashJob job(std::move(paths));
    QObject::connect(
        &job, &Fm::FileOperationJob::fileExists, &job,
        [&out](const Fm::FileInfo&, const Fm::FileInfo&, Fm::FileOperationJob::FileExistsAction& response,
               Fm::FilePath&) {
            ++out.promptCount;
            ++out.conflictCount;
            response = Fm::FileOperationJob::SKIP;
        },
        Qt::DirectConnection);

    job.run();

    out.cancelled = job.isCancelled();
    out.success = !out.cancelled;
    out.sysErrno = out.cancelled ? ECANCELED : 0;
    captureLibfmProgress(job, out);
    ensureProgressSample(out);
    return out;
}

}  // namespace

class CrossLayerParityTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void copyPromptSkipParityAcrossCoreQtAndLibfmQt();
    void copyPromptCancelParityAcrossCoreQtAndLibfmQt();
    void deleteParityAcrossCoreQtAndLibfmQt();
    void trashAndUntrashParityAcrossCoreAndLibfmQt();
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

    verifySuccessCompletion(core);
    verifySuccessCompletion(qt);
    verifySuccessCompletion(libfm);

    QCOMPARE(core.promptCount, 1);
    QCOMPARE(qt.promptCount, 1);
    QCOMPARE(libfm.promptCount, 1);
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
    QVERIFY2(isProgressMonotonic(core.progressSamples), "Core progress must stay monotonic");
    QVERIFY2(isProgressMonotonic(qt.progressSamples), "Qt progress must stay monotonic");
    QVERIFY2(isProgressMonotonic(libfm.progressSamples), "libfm-qt progress must stay monotonic");

    QCOMPARE(core.promptCount, 1);
    QCOMPARE(qt.promptCount, 1);
    QCOMPARE(libfm.promptCount, 1);
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

void CrossLayerParityTest::deleteParityAcrossCoreQtAndLibfmQt() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray payload("delete-data");
    const QString corePath = writeLayerFile(dir.path() + QLatin1String("/core-delete"),
                                            QStringLiteral("delete-%1.txt").arg(uniqueToken()), payload);
    const QString qtPath = writeLayerFile(dir.path() + QLatin1String("/qt-delete"),
                                          QStringLiteral("delete-%1.txt").arg(uniqueToken()), payload);
    const QString libfmPath = writeLayerFile(dir.path() + QLatin1String("/libfm-delete"),
                                             QStringLiteral("delete-%1.txt").arg(uniqueToken()), payload);

    const LayerOutcome core = runCoreDelete(corePath);
    const LayerOutcome qt = runQtDelete(qtPath);
    const LayerOutcome libfm = runLibfmDelete(libfmPath);

    verifySuccessCompletion(core);
    verifySuccessCompletion(qt);
    verifySuccessCompletion(libfm);

    QCOMPARE(core.promptCount, 0);
    QCOMPARE(qt.promptCount, 0);
    QCOMPARE(libfm.promptCount, 0);
    QCOMPARE(core.conflictCount, 0);
    QCOMPARE(qt.conflictCount, 0);
    QCOMPARE(libfm.conflictCount, 0);

    QCOMPARE(core.sysErrno, 0);
    QCOMPARE(qt.sysErrno, 0);
    QCOMPARE(libfm.sysErrno, 0);
    QVERIFY(qt.errorMessage.isEmpty());

    QVERIFY(!QFileInfo::exists(corePath));
    QVERIFY(!QFileInfo::exists(qtPath));
    QVERIFY(!QFileInfo::exists(libfmPath));
}

void CrossLayerParityTest::trashAndUntrashParityAcrossCoreAndLibfmQt() {
    QTemporaryDir dir(QDir::homePath() + QLatin1String("/.cache/oneg4fm-cross-layer-trash-XXXXXX"));
    QVERIFY(dir.isValid());

    const QByteArray payload("trash-restore-data");

    QString listError;
    QSet<QString> trashBefore;
    if (!listTrashUris(trashBefore, listError)) {
        QSKIP(qPrintable(QStringLiteral("Skipping trash/untrash parity test: %1").arg(listError)));
    }

    const QString coreToken = uniqueToken();
    const QString coreSource =
        writeLayerFile(dir.path() + QLatin1String("/core"), QStringLiteral("trash-%1.txt").arg(coreToken), payload);
    const LayerOutcome coreTrash = runCoreTrash(coreSource);
    if (!coreTrash.success && (coreTrash.sysErrno == ENOTSUP || coreTrash.sysErrno == EOPNOTSUPP ||
                               coreTrash.errorMessage.contains(QStringLiteral("not supported"), Qt::CaseInsensitive))) {
        QSKIP(qPrintable(QStringLiteral("Skipping trash/untrash parity test: core trash unavailable (%1)")
                             .arg(coreTrash.errorMessage)));
    }
    verifySuccessCompletion(coreTrash);
    QCOMPARE(coreTrash.promptCount, 0);
    QCOMPARE(coreTrash.conflictCount, 0);
    QCOMPARE(coreTrash.sysErrno, 0);
    QVERIFY(!QFileInfo::exists(coreSource));

    QSet<QString> trashAfterCoreTrash;
    QVERIFY2(listTrashUris(trashAfterCoreTrash, listError), qPrintable(listError));
    const QString coreTrashUri = findAddedTrashUri(trashBefore, trashAfterCoreTrash, coreToken);
    QVERIFY2(!coreTrashUri.isEmpty(), "Unable to identify new core trash URI entry");

    const LayerOutcome coreUntrash = runCoreUntrash(coreTrashUri);
    verifySuccessCompletion(coreUntrash);
    QCOMPARE(coreUntrash.promptCount, 0);
    QCOMPARE(coreUntrash.conflictCount, 0);
    QCOMPARE(coreUntrash.sysErrno, 0);
    QVERIFY(QFileInfo::exists(coreSource));
    QCOMPARE(readFile(coreSource), payload);

    QSet<QString> trashAfterCoreUntrash;
    QVERIFY2(listTrashUris(trashAfterCoreUntrash, listError), qPrintable(listError));
    QVERIFY(!trashAfterCoreUntrash.contains(coreTrashUri));

    const QString libfmToken = uniqueToken();
    const QString libfmSource =
        writeLayerFile(dir.path() + QLatin1String("/libfm"), QStringLiteral("trash-%1.txt").arg(libfmToken), payload);
    const QSet<QString> trashBeforeLibfm = trashAfterCoreUntrash;

    const LayerOutcome libfmTrash = runLibfmTrash(libfmSource);
    if (!libfmTrash.success && (libfmTrash.sysErrno == ENOTSUP ||
                                libfmTrash.errorMessage.contains(QStringLiteral("unsupported"), Qt::CaseInsensitive))) {
        QSKIP(qPrintable(QStringLiteral("Skipping trash/untrash parity test: libfm-qt trash unavailable (%1)")
                             .arg(libfmTrash.errorMessage)));
    }
    verifySuccessCompletion(libfmTrash);
    QCOMPARE(libfmTrash.promptCount, 0);
    QCOMPARE(libfmTrash.conflictCount, 0);
    QCOMPARE(libfmTrash.sysErrno, 0);
    QVERIFY(!QFileInfo::exists(libfmSource));

    QSet<QString> trashAfterLibfmTrash;
    QVERIFY2(listTrashUris(trashAfterLibfmTrash, listError), qPrintable(listError));
    const QString libfmTrashUri = findAddedTrashUri(trashBeforeLibfm, trashAfterLibfmTrash, libfmToken);
    QVERIFY2(!libfmTrashUri.isEmpty(), "Unable to identify new libfm-qt trash URI entry");

    const LayerOutcome libfmUntrash = runLibfmUntrash(libfmTrashUri);
    verifySuccessCompletion(libfmUntrash);
    QCOMPARE(libfmUntrash.promptCount, 0);
    QCOMPARE(libfmUntrash.conflictCount, 0);
    QCOMPARE(libfmUntrash.sysErrno, 0);
    QVERIFY(QFileInfo::exists(libfmSource));
    QCOMPARE(readFile(libfmSource), payload);

    QSet<QString> trashAfterLibfmUntrash;
    QVERIFY2(listTrashUris(trashAfterLibfmUntrash, listError), qPrintable(listError));
    QVERIFY(!trashAfterLibfmUntrash.contains(libfmTrashUri));

    QCOMPARE(coreTrash.success, libfmTrash.success);
    QCOMPARE(coreTrash.cancelled, libfmTrash.cancelled);
    QCOMPARE(coreUntrash.success, libfmUntrash.success);
    QCOMPARE(coreUntrash.cancelled, libfmUntrash.cancelled);
}

QTEST_MAIN(CrossLayerParityTest)
#include "cross_layer_parity_test.moc"
