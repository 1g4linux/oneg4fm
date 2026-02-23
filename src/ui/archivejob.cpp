/*
 * Qt wrapper around the POSIX archive writer
 * src/ui/archivejob.cpp
 */

#include "archivejob.h"

#include "../core/archive_writer.h"

#include <QFile>
#include <QtConcurrent>
#include <string>
#include <vector>

namespace Oneg4FM {

ArchiveJob::ArchiveJob(QObject* parent) : QObject(parent), cancelRequested_(false) {}

void ArchiveJob::start(const QStringList& sourcePaths, const QString& destination) {
    cancelRequested_.store(false, std::memory_order_relaxed);

    auto future = QtConcurrent::run([this, sourcePaths, destination]() -> Result {
        std::vector<std::string> nativeSources;
        nativeSources.reserve(static_cast<std::size_t>(sourcePaths.size()));
        for (const auto& path : sourcePaths) {
            const QByteArray bytes = QFile::encodeName(path);
            nativeSources.emplace_back(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        }

        const QByteArray destBytes = QFile::encodeName(destination);
        const std::string nativeDest(destBytes.constData(), static_cast<std::size_t>(destBytes.size()));

        Oneg4FM::FsOps::ProgressInfo opProgress;
        Oneg4FM::FsOps::Error err;

        auto cb = [this](const Oneg4FM::FsOps::ProgressInfo& info) {
            if (cancelRequested_.load(std::memory_order_relaxed)) {
                return false;
            }
            QMetaObject::invokeMethod(
                this,
                [this, info]() {
                    Q_EMIT progress(info.bytesDone, info.bytesTotal, QString::fromLocal8Bit(info.currentPath.c_str()));
                },
                Qt::QueuedConnection);
            return true;
        };

        const bool ok = ArchiveWriter::create_tar_zst(nativeSources, nativeDest, opProgress, cb, err);
        Result result;
        result.success = ok;
        result.error = ok ? QString() : QString::fromLocal8Bit(err.message.c_str());
        return result;
    });

    connect(&watcher_, &QFutureWatcher<Result>::finished, this, &ArchiveJob::onFinished);
    watcher_.setFuture(future);
}

void ArchiveJob::cancel() {
    cancelRequested_.store(true, std::memory_order_relaxed);
}

void ArchiveJob::onFinished() {
    const Result result = watcher_.result();
    Q_EMIT finished(result.success, result.error);
}

}  // namespace Oneg4FM
