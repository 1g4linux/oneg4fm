/*
 * Qt-based file operations backend
 * src/backends/qt/qt_fileops.h
 */

#ifndef QT_FILEOPS_H
#define QT_FILEOPS_H

#include <QThread>

#include <atomic>
#include <memory>

#include "../../core/ifileops.h"

namespace PCManFM {

class QtFileOps : public IFileOps {
    Q_OBJECT

   public:
    explicit QtFileOps(QObject* parent = nullptr);
    ~QtFileOps() override;

    void start(const FileOpRequest& req) override;
    void cancel() override;
    void resolveConflict(FileOpConflictResolution resolution) override;

   private Q_SLOTS:
    void onWorkerConflict(const FileOpConflict& info);
    void onWorkerFinished(bool success, const QString& errorMessage);

   Q_SIGNALS:
    void startRequest(const FileOpRequest& req);
    void cancelRequest();

   private:
    bool hasConflictResponder() const;

    class Worker;
    std::shared_ptr<std::atomic<bool>> cancelRequested_;
    Worker* worker_;
    QThread* workerThread_;
};

}  // namespace PCManFM

#endif  // QT_FILEOPS_H
