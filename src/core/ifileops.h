/*
 * File operations interface
 * src/core/ifileops.h
 */

#ifndef IFILEOPS_H
#define IFILEOPS_H

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace Oneg4FM {

enum class FileOpType { Copy, Move, Delete };
enum class FileOpProgressPhase {
    Running,
    Finalizing,
};
enum class FileOpConflictResolution {
    Overwrite,
    Skip,
    Rename,
    Abort,
    OverwriteAll,
    SkipAll,
    RenameAll,
};

struct FileOpConflict {
    QString sourcePath;
    QString destinationPath;
    bool destinationIsDirectory = false;
};

struct FileOpRequest {
    FileOpType type = FileOpType::Copy;
    QStringList sources;
    // Destination directory for copy/move. Must be empty for delete.
    QString destination;
    bool followSymlinks = false;
    // For copy/move conflicts: true = overwrite, false = skip existing destination.
    // Must be false for delete.
    bool overwriteExisting = false;
    // For copy/move conflicts: true = ask UI for conflict decision per conflict event.
    // Must be false for delete and cannot be combined with overwriteExisting=true.
    bool promptOnConflict = false;
    bool preserveOwnership = false;
};

struct FileOpProgress {
    quint64 bytesDone;
    quint64 bytesTotal;
    int filesDone;
    int filesTotal;
    QString currentPath;
    FileOpProgressPhase phase = FileOpProgressPhase::Running;
};

class IFileOps : public QObject {
    Q_OBJECT

   public:
    using QObject::QObject;
    ~IFileOps() override = default;

    virtual void start(const FileOpRequest& req) = 0;
    virtual void cancel() = 0;
    virtual void resolveConflict(FileOpConflictResolution resolution) = 0;

   Q_SIGNALS:
    void progress(const FileOpProgress& info);
    void conflictRequested(const FileOpConflict& info);
    void finished(bool success, const QString& errorMessage);
};

}  // namespace Oneg4FM

Q_DECLARE_METATYPE(Oneg4FM::FileOpType)
Q_DECLARE_METATYPE(Oneg4FM::FileOpProgressPhase)
Q_DECLARE_METATYPE(Oneg4FM::FileOpConflictResolution)
Q_DECLARE_METATYPE(Oneg4FM::FileOpRequest)
Q_DECLARE_METATYPE(Oneg4FM::FileOpProgress)
Q_DECLARE_METATYPE(Oneg4FM::FileOpConflict)

#endif  // IFILEOPS_H
