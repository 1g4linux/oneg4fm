/*
 * Hex document model backed by low-level POSIX I/O
 * src/ui/hexdocument.h
 */

#ifndef PCMANFM_HEXDOCUMENT_H
#define PCMANFM_HEXDOCUMENT_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <memory>
#include <shared_mutex>

#include <cstdint>
#include <optional>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>

#include "../core/windowed_file_reader.h"

namespace Oneg4FM {

// HexDocument implements a paged piece-table style model to edit files without
// loading the whole file into memory. All disk access uses POSIX I/O calls.
class HexDocument : public QObject {
    Q_OBJECT

   public:
    enum class OperationType { Insert, Delete, Overwrite };

    struct Operation {
        OperationType type;
        std::uint64_t offset = 0;
        QByteArray oldData;
        QByteArray newData;
    };

    explicit HexDocument(QObject* parent = nullptr);
    ~HexDocument() override;

    bool openFile(const QString& path, QString& errorOut);
    bool save(QString& errorOut, bool ignoreExternalChange = false);
    bool saveAs(const QString& newPath, QString& errorOut);
    bool isRegularFile() const { return isRegular_; }

    QString path() const { return path_; }
    std::uint64_t size() const { return totalSize_; }
    bool modified() const { return dirty_; }

    bool readBytes(std::uint64_t offset, std::uint64_t length, QByteArray& out, QString& errorOut) const;
    bool readBytesWithMarkers(std::uint64_t offset,
                              std::uint64_t length,
                              QByteArray& out,
                              std::vector<bool>& modified,
                              QString& errorOut) const;
    bool hasExternalChange(bool& changed, QString& errorOut) const;
    bool currentFingerprint(quint64& fingerprintOut, QString& errorOut) const;
    bool reload(QString& errorOut);
    bool isModified(std::uint64_t offset) const;
    bool nextModifiedOffset(std::uint64_t startOffset, bool forward, std::uint64_t& foundOffset) const;
    bool findAll(const QByteArray& needle, std::vector<std::uint64_t>& offsets, QString& errorOut) const;
    bool findForward(const QByteArray& needle,
                     std::uint64_t startOffset,
                     std::uint64_t& foundOffset,
                     QString& errorOut) const;
    bool findBackward(const QByteArray& needle,
                      std::uint64_t startOffset,
                      std::uint64_t& foundOffset,
                      QString& errorOut) const;

    bool overwrite(std::uint64_t offset, const QByteArray& data, QString& errorOut);
    bool insert(std::uint64_t offset, const QByteArray& data, QString& errorOut);
    bool erase(std::uint64_t offset, std::uint64_t length, QString& errorOut);

    bool undo(QString& errorOut);
    bool redo(QString& errorOut);

   Q_SIGNALS:
    void changed();
    void saved();

   private:
    struct Segment {
        enum class Kind { Original, Added };
        Kind kind = Kind::Original;
        std::uint64_t sourceOffset = 0;
        std::uint64_t length = 0;
    };

    struct FileStat {
        dev_t dev = 0;
        ino_t ino = 0;
        std::uint64_t size = 0;
        std::int64_t mtimeSec = 0;
        std::int64_t mtimeNsec = 0;
        mode_t mode = 0;
        uid_t uid = 0;
        gid_t gid = 0;
    };

    bool loadStat(const QString& path, FileStat& st, QString& errorOut) const;
    bool openDescriptor(const QString& path, int flags, int& fdOut, QString& errorOut) const;
    void closeDescriptor(int& fd) const;

    bool ensureSplit(std::uint64_t offset, std::size_t& index, QString& errorOut);
    bool replaceRange(std::uint64_t offset,
                      std::uint64_t length,
                      const std::vector<Segment>& replacement,
                      QString& errorOut);
    bool appendAddedData(const QByteArray& data, std::uint64_t& startOffset, QString& errorOut);
    bool readFromSegments(std::uint64_t offset, std::uint64_t length, QByteArray& out, QString& errorOut) const;
    bool readOriginal(std::uint64_t offset, std::uint64_t length, QByteArray& out, QString& errorOut) const;

    bool applyOperation(const Operation& op, bool recordUndo, QString& errorOut, bool clearRedo = true);
    bool applyInverseAndPushRedo(const Operation& op, QString& errorOut);

    bool writeTempFile(const QString& destPath, QString& errorOut, QString& tempPathOut) const;
    bool streamLogicalToFd(int fd, QString& errorOut) const;
    bool saveInternal(QString& errorOut, bool ignoreExternalChange);
    bool rebuildFromCurrentFile(QString& errorOut);
    bool detectExternalChange(QString& errorOut) const;
    bool readOriginalUnlocked(std::uint64_t offset, std::uint64_t length, QByteArray& out, QString& errorOut) const;
    bool readBytesWithMarkersUnlocked(std::uint64_t offset,
                                      std::uint64_t length,
                                      QByteArray& out,
                                      std::vector<bool>& modified,
                                      QString& errorOut) const;
    bool findForwardUnlocked(const QByteArray& needle,
                             std::uint64_t startOffset,
                             std::uint64_t& foundOffset,
                             QString& errorOut) const;
    bool findBackwardUnlocked(const QByteArray& needle,
                              std::uint64_t startOffset,
                              std::uint64_t& foundOffset,
                              QString& errorOut) const;
    bool findAllUnlocked(const QByteArray& needle, std::vector<std::uint64_t>& offsets, QString& errorOut) const;
    bool isModifiedUnlocked(std::uint64_t offset) const;
    bool nextModifiedOffsetUnlocked(std::uint64_t startOffset, bool forward, std::uint64_t& foundOffset) const;

    int sourceFd_ = -1;
    QString path_;
    FileStat initialStat_{};
    FileStat currentStat_{};
    bool isRegular_ = true;
    std::uint64_t totalSize_ = 0;
    std::vector<Segment> segments_;
    QByteArray addedBuffer_;

    static constexpr std::size_t kReadWindowSize = 8 * 1024 * 1024;  // 8 MiB sliding mmap window
    std::unique_ptr<WindowedFileReader> reader_;
    mutable std::shared_mutex mutex_;

    std::vector<Operation> undoStack_;
    std::vector<Operation> redoStack_;
    bool dirty_ = false;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_HEXDOCUMENT_H
