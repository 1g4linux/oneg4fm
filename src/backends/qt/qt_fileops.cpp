/*
 * Qt-based file operations backend implementation
 * src/backends/qt/qt_fileops.cpp
 */

#include "qt_fileops.h"

#include "../../core/file_ops_contract.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <cerrno>

namespace PCManFM {

namespace {

std::string toNativePath(const QString& path) {
    const QByteArray bytes = QFile::encodeName(path);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString fromNativePath(const std::string& path) {
    return QString::fromLocal8Bit(path.c_str());
}

FileOpProgress toQtProgress(const FileOpsContract::ProgressSnapshot& core) {
    FileOpProgress qt{};
    qt.bytesDone = core.bytesDone;
    qt.bytesTotal = core.bytesTotal;
    qt.filesDone = core.filesDone;
    qt.filesTotal = core.filesTotal;
    qt.currentPath = fromNativePath(core.currentPath);
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

}  // namespace

class QtFileOps::Worker : public QObject {
    Q_OBJECT

   public:
    explicit Worker(QObject* parent = nullptr) : QObject(parent), cancelled_(false) {}

   public Q_SLOTS:
    void processRequest(const FileOpRequest& req) {
        cancelled_.store(false);

        FileOpsContract::Request contractReq;
        QString requestError;
        if (!buildContractRequest(req, contractReq, requestError)) {
            Q_EMIT finished(false, requestError);
            return;
        }

        contractReq.cancellationRequested = [this]() { return cancelled_.load(); };

        const FileOpsContract::EventHandlers handlers{
            [this](const FileOpsContract::ProgressSnapshot& info) { Q_EMIT progress(toQtProgress(info)); },
            {},
        };

        const FileOpsContract::Result result = FileOpsContract::run(contractReq, handlers);
        if (result.success) {
            Q_EMIT finished(true, QString());
            return;
        }

        if (result.cancelled || result.error.sysErrno == ECANCELED) {
            Q_EMIT finished(false, QStringLiteral("Operation cancelled"));
            return;
        }

        if (!result.error.message.empty()) {
            Q_EMIT finished(false, QString::fromLocal8Bit(result.error.message.c_str()));
            return;
        }

        Q_EMIT finished(false, QStringLiteral("Operation failed"));
    }

    void cancel() { cancelled_.store(true); }

   Q_SIGNALS:
    void progress(const FileOpProgress& info);
    void finished(bool success, const QString& errorMessage);

   private:
    bool buildContractRequest(const FileOpRequest& req, FileOpsContract::Request& out, QString& requestError) {
        out = {};
        requestError.clear();

        if (!toContractOperation(req.type, out.operation)) {
            Q_ASSERT_X(false, "QtFileOps::Worker::buildContractRequest", "Unknown file operation type");
            requestError = QStringLiteral("Unsupported file operation type");
            return false;
        }

        out.sources.reserve(static_cast<std::size_t>(req.sources.size()));
        for (const QString& source : req.sources) {
            if (source.isEmpty()) {
                requestError = QStringLiteral("Source path must not be empty");
                return false;
            }
            out.sources.push_back(toNativePath(source));
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
        }
        else {
            out.conflictPolicy = req.overwriteExisting ? FileOpsContract::ConflictPolicy::Overwrite
                                                       : FileOpsContract::ConflictPolicy::Skip;
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

        return true;
    }

    std::atomic<bool> cancelled_;
};

QtFileOps::QtFileOps(QObject* parent) : IFileOps(parent), worker_(new Worker), workerThread_(new QThread) {
    worker_->moveToThread(workerThread_);

    connect(this, &QtFileOps::startRequest, worker_, &Worker::processRequest);
    connect(this, &QtFileOps::cancelRequest, worker_, &Worker::cancel);
    connect(worker_, &Worker::progress, this, &QtFileOps::progress);
    connect(worker_, &Worker::finished, this, &QtFileOps::onWorkerFinished);

    workerThread_->start();
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
    Q_EMIT startRequest(req);
}

void QtFileOps::cancel() {
    Q_EMIT cancelRequest();
}

}  // namespace PCManFM

#include "qt_fileops.moc"
