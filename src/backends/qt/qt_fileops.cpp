/*
 * Qt-based file operations backend implementation
 * src/backends/qt/qt_fileops.cpp
 */

#include "qt_fileops.h"

#include "../../core/file_ops_contract.h"

#include <QFile>
#include <QMetaMethod>
#include <QObject>
#include <QThread>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace Oneg4FM {

namespace {

std::string toNativePath(const QString& path) {
    const QByteArray bytes = QFile::encodeName(path);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString fromNativePath(const std::string& path) {
    return QString::fromLocal8Bit(path.c_str());
}

FileOpProgressPhase toQtProgressPhase(FileOpsContract::ProgressPhase phase) {
    switch (phase) {
        case FileOpsContract::ProgressPhase::Running:
            return FileOpProgressPhase::Running;
        case FileOpsContract::ProgressPhase::Finalizing:
            return FileOpProgressPhase::Finalizing;
    }
    return FileOpProgressPhase::Running;
}

FileOpProgress toQtProgress(const FileOpsContract::ProgressSnapshot& core) {
    FileOpProgress qt{};
    qt.bytesDone = core.bytesDone;
    qt.bytesTotal = core.bytesTotal;
    qt.filesDone = core.filesDone;
    qt.filesTotal = core.filesTotal;
    qt.currentPath = fromNativePath(core.currentPath);
    qt.phase = toQtProgressPhase(core.phase);
    return qt;
}

FileOpConflict toQtConflict(const FileOpsContract::ConflictEvent& core) {
    FileOpConflict qt{};
    qt.sourcePath = fromNativePath(core.sourcePath);
    qt.destinationPath = fromNativePath(core.destinationPath);
    qt.destinationIsDirectory = core.destinationIsDirectory;
    return qt;
}

bool toContractOperation(FileOpType type, FileOpsContract::Operation& out) {
    switch (type) {
        case FileOpType::Copy:
            out = FileOpsContract::Operation::Copy;
            return true;
        case FileOpType::Move:
            out = FileOpsContract::Operation::Move;
            return true;
        case FileOpType::Delete:
            out = FileOpsContract::Operation::Delete;
            return true;
    }

    return false;
}

FileOpsContract::EndpointKind toContractEndpointKind(const QString& path) {
    return path.contains(QStringLiteral("://")) ? FileOpsContract::EndpointKind::Uri
                                                : FileOpsContract::EndpointKind::NativePath;
}

QString resultErrorMessage(const FileOpsContract::Result& result, const QString& fallback) {
    if (result.cancelled || result.error.sysErrno == ECANCELED) {
        return QStringLiteral("Operation cancelled");
    }

    if (!result.error.message.empty()) {
        return QString::fromLocal8Bit(result.error.message.c_str());
    }

    return fallback;
}

FileOpsContract::ConflictResolution toContractResolution(FileOpConflictResolution resolution) {
    switch (resolution) {
        case FileOpConflictResolution::Overwrite:
            return FileOpsContract::ConflictResolution::Overwrite;
        case FileOpConflictResolution::Skip:
            return FileOpsContract::ConflictResolution::Skip;
        case FileOpConflictResolution::Rename:
            return FileOpsContract::ConflictResolution::Rename;
        case FileOpConflictResolution::Abort:
            return FileOpsContract::ConflictResolution::Abort;
        case FileOpConflictResolution::OverwriteAll:
            return FileOpsContract::ConflictResolution::OverwriteAll;
        case FileOpConflictResolution::SkipAll:
            return FileOpsContract::ConflictResolution::SkipAll;
        case FileOpConflictResolution::RenameAll:
            return FileOpsContract::ConflictResolution::RenameAll;
    }

    return FileOpsContract::ConflictResolution::Abort;
}

bool sameProgressSnapshot(const FileOpProgress& lhs, const FileOpProgress& rhs) {
    return lhs.bytesDone == rhs.bytesDone && lhs.bytesTotal == rhs.bytesTotal && lhs.filesDone == rhs.filesDone &&
           lhs.filesTotal == rhs.filesTotal && lhs.currentPath == rhs.currentPath && lhs.phase == rhs.phase;
}

}  // namespace

class QtFileOps::Worker : public QObject {
    Q_OBJECT

   public:
    explicit Worker(QtFileOps* owner,
                    const std::shared_ptr<std::atomic<bool>>& cancelRequested,
                    QObject* parent = nullptr)
        : QObject(parent), owner_(owner), cancelRequested_(cancelRequested) {}

   public Q_SLOTS:
    void processRequest(const FileOpRequest& req) {
        resetProgressDispatchState();
        FileOpsContract::Request contractReq;
        QString requestError;
        if (!buildContractRequest(req, contractReq, requestError)) {
            Q_EMIT finished(false, requestError);
            return;
        }

        if (contractReq.conflictPolicy == FileOpsContract::ConflictPolicy::Prompt && !hasConflictResponder()) {
            Q_EMIT finished(false, QStringLiteral("Prompt conflict policy requires a connected conflict responder"));
            return;
        }

        const FileOpsContract::Result preflightResult = FileOpsContract::preflight(contractReq);
        if (!preflightResult.success) {
            Q_EMIT finished(false, resultErrorMessage(preflightResult, QStringLiteral("Operation failed")));
            return;
        }

        contractReq.cancellationRequested = [flag = cancelRequested_]() { return flag && flag->load(); };

        const FileOpsContract::EventHandlers handlers{
            [this](const FileOpsContract::ProgressSnapshot& info) { queueProgressUpdate(info); },
            [this](const FileOpsContract::ConflictEvent& event) { return waitForConflictResolution(event); },
        };

        const FileOpsContract::Result result = FileOpsContract::run(contractReq, handlers);
        flushPendingProgress();
        if (result.success) {
            Q_EMIT finished(true, QString());
            return;
        }

        Q_EMIT finished(false, resultErrorMessage(result, QStringLiteral("Operation failed")));
    }

    void cancel() {
        if (cancelRequested_) {
            cancelRequested_->store(true);
        }

        std::lock_guard<std::mutex> lock(conflictMutex_);
        if (!waitingForConflictResolution_) {
            return;
        }
        pendingConflictResolution_ = FileOpConflictResolution::Abort;
        hasConflictResolution_ = true;
        conflictCv_.notify_one();
    }

    void resolveConflict(FileOpConflictResolution resolution) {
        std::lock_guard<std::mutex> lock(conflictMutex_);
        if (!waitingForConflictResolution_) {
            return;
        }
        pendingConflictResolution_ = resolution;
        hasConflictResolution_ = true;
        conflictCv_.notify_one();
    }

   Q_SIGNALS:
    void progress(const FileOpProgress& info);
    void conflictRequested(const FileOpConflict& info);
    void finished(bool success, const QString& errorMessage);

   private:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kProgressEmitInterval = std::chrono::milliseconds(50);
    static constexpr std::uint64_t kProgressEmitByteDelta = 1024 * 1024;  // 1 MiB

    void resetProgressDispatchState() {
        std::lock_guard<std::mutex> lock(progressMutex_);
        pendingProgress_.reset();
        lastEmittedProgress_.reset();
        hasLastProgressEmitTime_ = false;
        progressDispatchQueued_ = false;
    }

    static bool isTerminalProgress(const FileOpProgress& info) {
        return info.phase == FileOpProgressPhase::Finalizing ||
               (info.filesTotal >= 0 && info.filesDone >= info.filesTotal && info.bytesDone >= info.bytesTotal);
    }

    bool shouldEmitProgressNowLocked(const FileOpProgress& info, const Clock::time_point now) const {
        if (!hasLastProgressEmitTime_) {
            return true;
        }
        if (isTerminalProgress(info)) {
            return true;
        }
        if (lastEmittedProgress_) {
            if (info.phase != lastEmittedProgress_->phase) {
                return true;
            }
            if (info.filesDone != lastEmittedProgress_->filesDone) {
                return true;
            }
            if (info.currentPath != lastEmittedProgress_->currentPath) {
                return true;
            }
            if (info.bytesDone >= lastEmittedProgress_->bytesDone + kProgressEmitByteDelta) {
                return true;
            }
        }
        return (now - lastProgressEmitTime_) >= kProgressEmitInterval;
    }

    void queueProgressUpdate(const FileOpsContract::ProgressSnapshot& info) {
        const FileOpProgress candidate = toQtProgress(info);
        bool scheduleDispatch = false;
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            pendingProgress_ = candidate;

            const Clock::time_point now = Clock::now();
            if (!progressDispatchQueued_ && shouldEmitProgressNowLocked(*pendingProgress_, now)) {
                progressDispatchQueued_ = true;
                scheduleDispatch = true;
            }
        }

        if (!scheduleDispatch || !owner_) {
            return;
        }

        QMetaObject::invokeMethod(
            owner_, [this]() { dispatchProgressOnOwnerThread(/*force=*/false); }, Qt::QueuedConnection);
    }

    void flushPendingProgress() {
        if (!owner_) {
            return;
        }
        QMetaObject::invokeMethod(
            owner_, [this]() { dispatchProgressOnOwnerThread(/*force=*/true); }, Qt::BlockingQueuedConnection);
    }

    void dispatchProgressOnOwnerThread(bool force) {
        std::optional<FileOpProgress> toEmit;
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressDispatchQueued_ = false;
            if (!pendingProgress_) {
                return;
            }

            const Clock::time_point now = Clock::now();
            if (!force && !shouldEmitProgressNowLocked(*pendingProgress_, now)) {
                return;
            }

            const FileOpProgress info = *pendingProgress_;
            pendingProgress_.reset();

            if (lastEmittedProgress_ && sameProgressSnapshot(info, *lastEmittedProgress_)) {
                return;
            }

            toEmit = info;
            lastEmittedProgress_ = info;
            hasLastProgressEmitTime_ = true;
            lastProgressEmitTime_ = now;
        }

        if (!toEmit.has_value()) {
            return;
        }
        Q_EMIT progress(*toEmit);
    }

    bool hasConflictResponder() const {
        if (!owner_) {
            return false;
        }

        bool responderConnected = false;
        QMetaObject::invokeMethod(
            owner_, [this, &responderConnected]() { responderConnected = owner_->hasConflictResponder(); },
            Qt::BlockingQueuedConnection);
        return responderConnected;
    }

    FileOpsContract::ConflictResolution waitForConflictResolution(const FileOpsContract::ConflictEvent& event) {
        {
            std::lock_guard<std::mutex> lock(conflictMutex_);
            waitingForConflictResolution_ = true;
            hasConflictResolution_ = false;
            pendingConflictResolution_ = FileOpConflictResolution::Abort;
        }

        Q_EMIT conflictRequested(toQtConflict(event));

        std::unique_lock<std::mutex> lock(conflictMutex_);
        conflictCv_.wait(lock, [this]() { return hasConflictResolution_; });

        const FileOpConflictResolution resolution = pendingConflictResolution_;
        hasConflictResolution_ = false;
        waitingForConflictResolution_ = false;
        return toContractResolution(resolution);
    }

    bool buildContractRequest(const FileOpRequest& req, FileOpsContract::Request& out, QString& requestError) {
        out = {};
        requestError.clear();
        bool hasUriEndpoint = false;

        if (!toContractOperation(req.type, out.operation)) {
            Q_ASSERT_X(false, "QtFileOps::Worker::buildContractRequest", "Unknown file operation type");
            requestError = QStringLiteral("Unsupported file operation type");
            return false;
        }

        out.sources.reserve(static_cast<std::size_t>(req.sources.size()));
        out.routing.sourceKinds.reserve(static_cast<std::size_t>(req.sources.size()));
        for (const QString& source : req.sources) {
            if (source.isEmpty()) {
                requestError = QStringLiteral("Source path must not be empty");
                return false;
            }
            out.sources.push_back(toNativePath(source));
            out.routing.sourceKinds.push_back(toContractEndpointKind(source));
            if (out.routing.sourceKinds.back() == FileOpsContract::EndpointKind::Uri) {
                hasUriEndpoint = true;
            }
        }

        if (out.sources.empty()) {
            requestError = QStringLiteral("At least one source path is required");
            return false;
        }

        if (req.type != FileOpType::Delete) {
            if (req.destination.isEmpty()) {
                requestError = QStringLiteral("Destination path is required");
                return false;
            }
            out.destination.targetDir = toNativePath(req.destination);
            out.destination.mappingMode = FileOpsContract::DestinationMappingMode::SourceBasename;
            out.routing.destinationKind = toContractEndpointKind(req.destination);
            if (out.routing.destinationKind == FileOpsContract::EndpointKind::Uri) {
                hasUriEndpoint = true;
            }
        }
        else if (!req.destination.isEmpty()) {
            requestError = QStringLiteral("Delete operations do not accept a destination path");
            return false;
        }

        if (req.type == FileOpType::Delete) {
            if (req.overwriteExisting) {
                requestError = QStringLiteral("Delete operations do not use overwriteExisting");
                return false;
            }
            if (req.promptOnConflict) {
                requestError = QStringLiteral("Delete operations do not use promptOnConflict");
                return false;
            }
        }
        else {
            if (req.promptOnConflict && req.overwriteExisting) {
                requestError = QStringLiteral("promptOnConflict cannot be combined with overwriteExisting");
                return false;
            }

            if (req.promptOnConflict) {
                out.conflictPolicy = FileOpsContract::ConflictPolicy::Prompt;
            }
            else {
                out.conflictPolicy = req.overwriteExisting ? FileOpsContract::ConflictPolicy::Overwrite
                                                           : FileOpsContract::ConflictPolicy::Skip;
            }
        }

        if (req.followSymlinks) {
            requestError = QStringLiteral("followSymlinks=true is not supported");
            return false;
        }

        out.symlinkPolicy.followSymlinks = false;
        out.symlinkPolicy.copyMode = FileOpsContract::SymlinkCopyMode::CopyLinkAsLink;

        out.metadata.preserveOwnership = req.preserveOwnership;
        out.metadata.preservePermissions = true;
        out.metadata.preserveTimestamps = true;

        out.atomicity.requireAtomicReplace = false;
        out.atomicity.bestEffortAtomicMove = true;

        out.cancelGranularity = FileOpsContract::CancelCheckpointGranularity::PerChunk;
        out.linuxSafety.requireOpenat2Resolve = true;
        out.linuxSafety.requireLandlock = false;
        out.routing.defaultBackend = hasUriEndpoint ? FileOpsContract::Backend::Gio : FileOpsContract::Backend::Auto;
        out.routing.destinationBackend = FileOpsContract::Backend::Auto;

        return true;
    }

    QtFileOps* owner_;
    std::shared_ptr<std::atomic<bool>> cancelRequested_;
    std::mutex conflictMutex_;
    std::condition_variable conflictCv_;
    bool waitingForConflictResolution_ = false;
    bool hasConflictResolution_ = false;
    FileOpConflictResolution pendingConflictResolution_ = FileOpConflictResolution::Abort;
    std::mutex progressMutex_;
    std::optional<FileOpProgress> pendingProgress_;
    std::optional<FileOpProgress> lastEmittedProgress_;
    Clock::time_point lastProgressEmitTime_{};
    bool hasLastProgressEmitTime_ = false;
    bool progressDispatchQueued_ = false;
};

QtFileOps::QtFileOps(QObject* parent)
    : IFileOps(parent),
      cancelRequested_(std::make_shared<std::atomic<bool>>(false)),
      worker_(new Worker(this, cancelRequested_)),
      workerThread_(new QThread) {
    qRegisterMetaType<FileOpRequest>("Oneg4FM::FileOpRequest");
    qRegisterMetaType<FileOpProgress>("Oneg4FM::FileOpProgress");
    qRegisterMetaType<FileOpProgressPhase>("Oneg4FM::FileOpProgressPhase");
    qRegisterMetaType<FileOpConflict>("Oneg4FM::FileOpConflict");
    qRegisterMetaType<FileOpConflictResolution>("Oneg4FM::FileOpConflictResolution");

    worker_->moveToThread(workerThread_);

    connect(this, &QtFileOps::startRequest, worker_, &Worker::processRequest);
    connect(this, &QtFileOps::cancelRequest, worker_, &Worker::cancel);
    connect(worker_, &Worker::progress, this, &QtFileOps::progress);
    connect(worker_, &Worker::conflictRequested, this, &QtFileOps::onWorkerConflict);
    connect(worker_, &Worker::finished, this, &QtFileOps::onWorkerFinished);

    workerThread_->start();
}

bool QtFileOps::hasConflictResponder() const {
    return isSignalConnected(QMetaMethod::fromSignal(&IFileOps::conflictRequested));
}

void QtFileOps::onWorkerConflict(const FileOpConflict& info) {
    if (!hasConflictResponder()) {
        resolveConflict(FileOpConflictResolution::Abort);
        return;
    }

    Q_EMIT conflictRequested(info);
}

void QtFileOps::onWorkerFinished(bool success, const QString& errorMessage) {
    Q_EMIT finished(success, errorMessage);
}

QtFileOps::~QtFileOps() {
    cancel();
    workerThread_->quit();
    workerThread_->wait();
    delete worker_;
    delete workerThread_;
}

void QtFileOps::start(const FileOpRequest& req) {
    cancelRequested_->store(false);
    Q_EMIT startRequest(req);
}

void QtFileOps::cancel() {
    cancelRequested_->store(true);
    Q_EMIT cancelRequest();
}

void QtFileOps::resolveConflict(FileOpConflictResolution resolution) {
    if (!worker_) {
        return;
    }
    worker_->resolveConflict(resolution);
}

}  // namespace Oneg4FM

#include "qt_fileops.moc"
