/*
 * Qt wrapper around the POSIX archive writer
 * src/ui/archivejob.h
 */

#ifndef PCMANFM_ARCHIVEJOB_H
#define PCMANFM_ARCHIVEJOB_H

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>

#include <atomic>

namespace Oneg4FM {

class ArchiveJob : public QObject {
    Q_OBJECT
   public:
    explicit ArchiveJob(QObject* parent = nullptr);

    // Starts the archive creation asynchronously. Paths are expected to be native, absolute, or
    // otherwise valid for the filesystem; the job converts them with QFile::encodeName.
    void start(const QStringList& sourcePaths, const QString& destination);
    void cancel();

   Q_SIGNALS:
    void progress(quint64 bytesDone, quint64 bytesTotal, const QString& currentPath);
    void finished(bool success, const QString& errorMessage);

   private:
    struct Result {
        bool success = false;
        QString error;
    };

    void onFinished();

    QFutureWatcher<Result> watcher_;
    std::atomic<bool> cancelRequested_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_ARCHIVEJOB_H
