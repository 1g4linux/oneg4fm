/*
 * Qt wrapper for archive extraction
 * src/ui/archiveextractjob.h
 */

#ifndef PCMANFM_ARCHIVEEXTRACTJOB_H
#define PCMANFM_ARCHIVEEXTRACTJOB_H

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>

namespace Oneg4FM {

class ArchiveExtractJob : public QObject {
    Q_OBJECT
   public:
    explicit ArchiveExtractJob(QObject* parent = nullptr);

    void start(const QString& archivePath, const QString& destinationDir);
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

#endif  // PCMANFM_ARCHIVEEXTRACTJOB_H
