/*
 * Qt wrapper for archive extraction
 * src/ui/archiveextractjob.cpp
 */

#include "archiveextractjob.h"

#include "../core/archive_extract.h"
#include "../core/fs_ops.h"

#include <QFile>
#include <QtConcurrent>
#include <string>
#include <vector>

namespace Oneg4FM {

ArchiveExtractJob::ArchiveExtractJob(QObject* parent) : QObject(parent), cancelRequested_(false) {}

void ArchiveExtractJob::start(const QString& archivePath, const QString& destinationDir) {
    cancelRequested_.store(false, std::memory_order_relaxed);

    auto future = QtConcurrent::run([this, archivePath, destinationDir]() -> Result {
        const QByteArray archiveBytes = QFile::encodeName(archivePath);
        const QByteArray destBytes = QFile::encodeName(destinationDir);

        const std::string archiveNative(archiveBytes.constData(), static_cast<std::size_t>(archiveBytes.size()));
        const std::string destNative(destBytes.constData(), static_cast<std::size_t>(destBytes.size()));

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

        Oneg4FM::ArchiveExtract::Options opts;
        // Use all available cores for filters when libarchive supports it.
        opts.enableFilterThreads = true;
        opts.maxFilterThreads = 0;

        const bool ok = Oneg4FM::ArchiveExtract::extract_archive(archiveNative, destNative, opProgress, cb, err, opts);

        Result result;
        result.success = ok;
        result.error = ok ? QString() : QString::fromLocal8Bit(err.message.c_str());
        return result;
    });

    connect(&watcher_, &QFutureWatcher<Result>::finished, this, &ArchiveExtractJob::onFinished);
    watcher_.setFuture(future);
}

void ArchiveExtractJob::cancel() {
    cancelRequested_.store(true, std::memory_order_relaxed);
}

void ArchiveExtractJob::onFinished() {
    const Result result = watcher_.result();
    Q_EMIT finished(result.success, result.error);
}

}  // namespace Oneg4FM
