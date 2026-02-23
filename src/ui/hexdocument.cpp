/*
 * Hex document model backed by low-level POSIX I/O
 * src/ui/hexdocument.cpp
 */

#include "hexdocument.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <cstddef>
#include <string>
#include <exception>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace Oneg4FM {

namespace {

QString errnoString(const char* context) {
    return QString::fromLocal8Bit(context) + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno));
}

bool writeAll(int fd, const char* data, std::size_t size, QString& errorOut) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            errorOut = errnoString("write");
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

HexDocument::HexDocument(QObject* parent) : QObject(parent) {}

HexDocument::~HexDocument() {
    closeDescriptor(sourceFd_);
}

bool HexDocument::loadStat(const QString& path, FileStat& st, QString& errorOut) const {
    const QByteArray native = QFile::encodeName(path);
    struct stat sb{};
    if (::lstat(native.constData(), &sb) < 0) {
        errorOut = errnoString("lstat");
        return false;
    }
    st.dev = sb.st_dev;
    st.ino = sb.st_ino;
    st.size = static_cast<std::uint64_t>(sb.st_size);
    st.mtimeSec = static_cast<std::int64_t>(sb.st_mtim.tv_sec);
    st.mtimeNsec = static_cast<std::int64_t>(sb.st_mtim.tv_nsec);
    st.mode = sb.st_mode;
    st.uid = sb.st_uid;
    st.gid = sb.st_gid;
    return true;
}

bool HexDocument::openDescriptor(const QString& path, int flags, int& fdOut, QString& errorOut) const {
    const QByteArray native = QFile::encodeName(path);
    int fd = ::open(native.constData(), flags, 0);
    if (fd < 0) {
        errorOut = errnoString("open");
        return false;
    }
    fdOut = fd;
    return true;
}

void HexDocument::closeDescriptor(int& fd) const {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool HexDocument::openFile(const QString& path, QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    closeDescriptor(sourceFd_);
    segments_.clear();
    addedBuffer_.clear();
    undoStack_.clear();
    redoStack_.clear();
    totalSize_ = 0;
    dirty_ = false;
    reader_.reset();

    FileStat st;
    if (!loadStat(path, st, errorOut)) {
        return false;
    }

    if (S_ISLNK(st.mode)) {
        errorOut = tr("Editing symlinks is disabled for safety.");
        return false;
    }
    if (!S_ISREG(st.mode)) {
        errorOut = tr("Only regular files can be opened in the hex editor.");
        return false;
    }

    int fd = -1;
    if (!openDescriptor(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, fd, errorOut)) {
        return false;
    }

    std::string readerErr;
    reader_ = std::make_unique<WindowedFileReader>(path.toStdString(), kReadWindowSize, &readerErr);
    if (!reader_ || !reader_->valid()) {
        errorOut = QString::fromLocal8Bit(readerErr.c_str());
        ::close(fd);
        reader_.reset();
        return false;
    }

    sourceFd_ = fd;
    path_ = path;
    initialStat_ = st;
    currentStat_ = st;
    totalSize_ = st.size;
    isRegular_ = S_ISREG(st.mode);

    if (totalSize_ > 0) {
        Segment seg;
        seg.kind = Segment::Kind::Original;
        seg.sourceOffset = 0;
        seg.length = totalSize_;
        segments_.push_back(seg);
    }

    lock.unlock();
    Q_EMIT changed();
    return true;
}

bool HexDocument::ensureSplit(std::uint64_t offset, std::size_t& index, QString& errorOut) {
    if (offset > totalSize_) {
        errorOut = tr("Offset %1 is past the end of the file.").arg(offset);
        return false;
    }

    std::uint64_t pos = 0;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const Segment& seg = segments_[i];
        if (offset == pos) {
            index = i;
            return true;
        }
        if (offset < pos + seg.length) {
            // Split the segment
            const std::uint64_t leftLen = offset - pos;
            const std::uint64_t rightLen = seg.length - leftLen;
            Segment left = seg;
            Segment right = seg;
            left.length = leftLen;
            right.sourceOffset += leftLen;
            right.length = rightLen;
            segments_[i] = left;
            segments_.insert(segments_.begin() + static_cast<std::ptrdiff_t>(i + 1), right);
            index = i + 1;
            return true;
        }
        pos += seg.length;
    }

    index = segments_.size();
    return true;
}

bool HexDocument::replaceRange(std::uint64_t offset,
                               std::uint64_t length,
                               const std::vector<Segment>& replacement,
                               QString& errorOut) {
    if (offset > totalSize_) {
        errorOut = tr("Offset %1 is past the end of the file.").arg(offset);
        return false;
    }
    if (offset + length > totalSize_) {
        length = totalSize_ - offset;
    }

    std::size_t startIdx = 0;
    std::size_t endIdx = 0;
    if (!ensureSplit(offset, startIdx, errorOut)) {
        return false;
    }
    if (!ensureSplit(offset + length, endIdx, errorOut)) {
        return false;
    }

    segments_.erase(segments_.begin() + static_cast<std::ptrdiff_t>(startIdx),
                    segments_.begin() + static_cast<std::ptrdiff_t>(endIdx));
    segments_.insert(segments_.begin() + static_cast<std::ptrdiff_t>(startIdx), replacement.begin(), replacement.end());

    // Merge adjacent compatible segments to keep the list small
    std::vector<Segment> merged;
    merged.reserve(segments_.size());
    for (const auto& seg : segments_) {
        if (seg.length == 0) {
            continue;
        }
        if (!merged.empty()) {
            Segment& back = merged.back();
            if (back.kind == seg.kind) {
                const std::uint64_t backEnd = back.sourceOffset + back.length;
                if (backEnd == seg.sourceOffset) {
                    back.length += seg.length;
                    continue;
                }
            }
        }
        merged.push_back(seg);
    }
    segments_.swap(merged);

    std::uint64_t newSize = 0;
    for (const auto& seg : segments_) {
        newSize += seg.length;
    }
    totalSize_ = newSize;
    return true;
}

bool HexDocument::appendAddedData(const QByteArray& data, std::uint64_t& startOffset, QString& errorOut) {
    Q_UNUSED(errorOut);
    startOffset = static_cast<std::uint64_t>(addedBuffer_.size());
    addedBuffer_.append(data);
    return true;
}

bool HexDocument::readOriginal(std::uint64_t offset, std::uint64_t length, QByteArray& out, QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return readOriginalUnlocked(offset, length, out, errorOut);
}

bool HexDocument::readOriginalUnlocked(std::uint64_t offset,
                                       std::uint64_t length,
                                       QByteArray& out,
                                       QString& errorOut) const {
    if (length == 0) {
        out.clear();
        return true;
    }
    if (!reader_) {
        errorOut = tr("File reader is not available.");
        return false;
    }

    out.resize(static_cast<int>(length));
    std::size_t bytesRead = 0;
    std::string err;
    if (!reader_->read(offset, static_cast<std::size_t>(length), reinterpret_cast<std::uint8_t*>(out.data()), bytesRead,
                       err)) {
        errorOut = QString::fromLocal8Bit(err.c_str());
        return false;
    }
    if (bytesRead < static_cast<std::size_t>(length)) {
        out.resize(static_cast<int>(bytesRead));
    }
    return true;
}

bool HexDocument::readFromSegments(std::uint64_t offset,
                                   std::uint64_t length,
                                   QByteArray& out,
                                   QString& errorOut) const {
    std::vector<bool> modified;
    return readBytesWithMarkersUnlocked(offset, length, out, modified, errorOut);
}

bool HexDocument::readBytes(std::uint64_t offset, std::uint64_t length, QByteArray& out, QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<bool> modified;
    return readBytesWithMarkersUnlocked(offset, length, out, modified, errorOut);
}

bool HexDocument::hasExternalChange(bool& changed, QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    changed = false;
    if (path_.isEmpty()) {
        return true;
    }
    FileStat st;
    if (!loadStat(path_, st, errorOut)) {
        return false;
    }
    changed = st.dev != initialStat_.dev || st.ino != initialStat_.ino || st.size != initialStat_.size ||
              st.mtimeSec != initialStat_.mtimeSec || st.mtimeNsec != initialStat_.mtimeNsec;
    return true;
}

bool HexDocument::currentFingerprint(quint64& fingerprintOut, QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    fingerprintOut = 0;
    if (path_.isEmpty()) {
        return false;
    }
    FileStat st;
    if (!loadStat(path_, st, errorOut)) {
        return false;
    }
    const quint64 upper = (static_cast<quint64>(st.mtimeSec) << 32) ^ static_cast<quint64>(st.mtimeNsec);
    fingerprintOut = upper ^ static_cast<quint64>(st.size) ^ static_cast<quint64>(st.ino);
    return true;
}

bool HexDocument::reload(QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (path_.isEmpty()) {
        errorOut = tr("No file is currently open.");
        return false;
    }
    const bool ok = rebuildFromCurrentFile(errorOut);
    lock.unlock();
    if (ok) {
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::readBytesWithMarkers(std::uint64_t offset,
                                       std::uint64_t length,
                                       QByteArray& out,
                                       std::vector<bool>& modified,
                                       QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return readBytesWithMarkersUnlocked(offset, length, out, modified, errorOut);
}

bool HexDocument::readBytesWithMarkersUnlocked(std::uint64_t offset,
                                               std::uint64_t length,
                                               QByteArray& out,
                                               std::vector<bool>& modified,
                                               QString& errorOut) const {
    if (offset >= totalSize_) {
        out.clear();
        return true;
    }
    const std::uint64_t maxReadable = totalSize_ - offset;
    if (length > maxReadable) {
        length = maxReadable;
    }
    out.resize(static_cast<int>(length));

    modified.assign(static_cast<std::size_t>(length), false);

    std::uint64_t pos = 0;
    std::size_t outPos = 0;
    for (const auto& seg : segments_) {
        if (offset >= pos + seg.length) {
            pos += seg.length;
            continue;
        }

        const std::uint64_t localStart = offset > pos ? offset - pos : 0;
        const std::uint64_t avail = seg.length - localStart;
        const std::uint64_t toCopy = std::min<std::uint64_t>(avail, length - outPos);

        if (seg.kind == Segment::Kind::Original) {
            QByteArray chunk;
            if (!readOriginalUnlocked(seg.sourceOffset + localStart, toCopy, chunk, errorOut)) {
                return false;
            }
            std::memcpy(out.data() + static_cast<int>(outPos), chunk.constData(), static_cast<std::size_t>(toCopy));
        }
        else {
            const std::uint64_t start = seg.sourceOffset + localStart;
            std::memcpy(out.data() + static_cast<int>(outPos), addedBuffer_.constData() + start,
                        static_cast<std::size_t>(toCopy));
            std::fill(modified.begin() + static_cast<std::ptrdiff_t>(outPos),
                      modified.begin() + static_cast<std::ptrdiff_t>(outPos + toCopy), true);
        }

        outPos += static_cast<std::size_t>(toCopy);
        if (outPos >= length) {
            break;
        }
        pos += seg.length;
    }

    if (outPos < length) {
        out.truncate(static_cast<int>(outPos));
        modified.resize(outPos);
    }
    return true;
}

bool HexDocument::applyOperation(const Operation& op, bool recordUndo, QString& errorOut, bool clearRedo) {
    std::uint64_t removeLen = 0;
    QByteArray replacement;

    switch (op.type) {
        case OperationType::Insert:
            removeLen = 0;
            replacement = op.newData;
            break;
        case OperationType::Delete:
            removeLen = op.oldData.size();
            replacement.clear();
            break;
        case OperationType::Overwrite:
            removeLen = op.oldData.size();
            replacement = op.newData;
            break;
    }

    std::vector<Segment> repl;
    if (!replacement.isEmpty()) {
        std::uint64_t addedOffset = 0;
        if (!appendAddedData(replacement, addedOffset, errorOut)) {
            return false;
        }
        Segment seg;
        seg.kind = Segment::Kind::Added;
        seg.sourceOffset = addedOffset;
        seg.length = static_cast<std::uint64_t>(replacement.size());
        repl.push_back(seg);
    }

    if (!replaceRange(op.offset, removeLen, repl, errorOut)) {
        return false;
    }

    if (recordUndo) {
        undoStack_.push_back(op);
        if (clearRedo) {
            redoStack_.clear();
        }
    }

    dirty_ = true;
    return true;
}

bool HexDocument::applyInverseAndPushRedo(const Operation& op, QString& errorOut) {
    Operation inverse;
    switch (op.type) {
        case OperationType::Insert:
            inverse.type = OperationType::Delete;
            inverse.offset = op.offset;
            inverse.oldData = op.newData;
            break;
        case OperationType::Delete:
            inverse.type = OperationType::Insert;
            inverse.offset = op.offset;
            inverse.newData = op.oldData;
            break;
        case OperationType::Overwrite:
            inverse.type = OperationType::Overwrite;
            inverse.offset = op.offset;
            inverse.oldData = op.newData;
            inverse.newData = op.oldData;
            break;
    }

    // Apply inverse without modifying undo stack
    if (!applyOperation(inverse, /*recordUndo=*/false, errorOut)) {
        return false;
    }

    redoStack_.push_back(op);
    return true;
}

bool HexDocument::overwrite(std::uint64_t offset, const QByteArray& data, QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (data.isEmpty()) {
        return true;
    }
    if (offset > totalSize_) {
        errorOut = tr("Offset %1 is past the end of the file.").arg(offset);
        return false;
    }

    const std::uint64_t available = totalSize_ - offset;
    const std::uint64_t removeLen = std::min<std::uint64_t>(available, static_cast<std::uint64_t>(data.size()));

    QByteArray oldData;
    if (!readFromSegments(offset, removeLen, oldData, errorOut)) {
        return false;
    }

    Operation op;
    op.type = OperationType::Overwrite;
    op.offset = offset;
    op.oldData = oldData;
    op.newData = data;

    const bool ok = applyOperation(op, /*recordUndo=*/true, errorOut);
    lock.unlock();
    if (ok) {
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::insert(std::uint64_t offset, const QByteArray& data, QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (data.isEmpty()) {
        return true;
    }
    if (offset > totalSize_) {
        errorOut = tr("Offset %1 is past the end of the file.").arg(offset);
        return false;
    }

    Operation op;
    op.type = OperationType::Insert;
    op.offset = offset;
    op.newData = data;
    const bool ok = applyOperation(op, /*recordUndo=*/true, errorOut);
    lock.unlock();
    if (ok) {
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::erase(std::uint64_t offset, std::uint64_t length, QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (length == 0) {
        return true;
    }
    if (offset >= totalSize_) {
        return true;
    }
    const std::uint64_t removeLen = std::min<std::uint64_t>(length, totalSize_ - offset);
    QByteArray oldData;
    if (!readFromSegments(offset, removeLen, oldData, errorOut)) {
        return false;
    }

    Operation op;
    op.type = OperationType::Delete;
    op.offset = offset;
    op.oldData = oldData;

    const bool ok = applyOperation(op, /*recordUndo=*/true, errorOut);
    lock.unlock();
    if (ok) {
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::undo(QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (undoStack_.empty()) {
        return true;
    }
    const Operation op = undoStack_.back();
    undoStack_.pop_back();
    const bool ok = applyInverseAndPushRedo(op, errorOut);
    lock.unlock();
    if (ok) {
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::redo(QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (redoStack_.empty()) {
        return true;
    }
    const Operation op = redoStack_.back();
    redoStack_.pop_back();
    const bool ok = applyOperation(op, /*recordUndo=*/true, errorOut, /*clearRedo=*/false);
    lock.unlock();
    if (ok) {
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::findForward(const QByteArray& needle,
                              std::uint64_t startOffset,
                              std::uint64_t& foundOffset,
                              QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return findForwardUnlocked(needle, startOffset, foundOffset, errorOut);
}

bool HexDocument::findForwardUnlocked(const QByteArray& needle,
                                      std::uint64_t startOffset,
                                      std::uint64_t& foundOffset,
                                      QString& errorOut) const {
    foundOffset = 0;
    if (needle.isEmpty()) {
        errorOut = tr("Search pattern cannot be empty.");
        return false;
    }
    if (startOffset > totalSize_) {
        startOffset = totalSize_;
    }

    constexpr std::uint64_t kWindow = 4096 * 4;
    const std::uint64_t needleSize = static_cast<std::uint64_t>(needle.size());
    std::uint64_t pos = startOffset;
    while (pos < totalSize_) {
        const std::uint64_t remaining = totalSize_ - pos;
        const std::uint64_t readLen = std::min<std::uint64_t>(remaining, kWindow + needleSize);
        QByteArray buffer;
        if (!readFromSegments(pos, readLen, buffer, errorOut)) {
            return false;
        }
        const int idx = buffer.indexOf(needle);
        if (idx >= 0) {
            foundOffset = pos + static_cast<std::uint64_t>(idx);
            return true;
        }
        if (readLen <= needleSize) {
            break;
        }
        pos += readLen - needleSize;
    }
    return false;
}

bool HexDocument::findBackward(const QByteArray& needle,
                               std::uint64_t startOffset,
                               std::uint64_t& foundOffset,
                               QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return findBackwardUnlocked(needle, startOffset, foundOffset, errorOut);
}

bool HexDocument::findBackwardUnlocked(const QByteArray& needle,
                                       std::uint64_t startOffset,
                                       std::uint64_t& foundOffset,
                                       QString& errorOut) const {
    foundOffset = 0;
    if (needle.isEmpty()) {
        errorOut = tr("Search pattern cannot be empty.");
        return false;
    }
    const std::uint64_t needleSize = static_cast<std::uint64_t>(needle.size());
    if (startOffset > totalSize_) {
        startOffset = totalSize_;
    }

    constexpr std::uint64_t kWindow = 4096 * 4;
    std::uint64_t pos = (startOffset > 0) ? startOffset - 1 : 0;

    while (true) {
        const std::uint64_t begin = (pos > kWindow) ? pos - kWindow : 0;
        const std::uint64_t length = pos - begin + needleSize;

        QByteArray buffer;
        if (!readFromSegments(begin, length, buffer, errorOut)) {
            return false;
        }
        const int idx = buffer.lastIndexOf(needle);
        if (idx >= 0) {
            foundOffset = begin + static_cast<std::uint64_t>(idx);
            return true;
        }

        if (begin == 0) {
            break;
        }
        if (begin < needleSize) {
            break;
        }
        pos = begin - needleSize;
    }
    return false;
}

bool HexDocument::findAll(const QByteArray& needle, std::vector<std::uint64_t>& offsets, QString& errorOut) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return findAllUnlocked(needle, offsets, errorOut);
}

bool HexDocument::findAllUnlocked(const QByteArray& needle,
                                  std::vector<std::uint64_t>& offsets,
                                  QString& errorOut) const {
    offsets.clear();
    if (needle.isEmpty()) {
        errorOut = tr("Search pattern cannot be empty.");
        return false;
    }
    std::uint64_t pos = 0;
    while (pos < totalSize_) {
        std::uint64_t found = 0;
        if (!findForwardUnlocked(needle, pos, found, errorOut)) {
            if (!errorOut.isEmpty()) {
                return false;
            }
            break;
        }
        offsets.push_back(found);
        pos = found + static_cast<std::uint64_t>(needle.size());
    }
    return true;
}

bool HexDocument::isModified(std::uint64_t offset) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return isModifiedUnlocked(offset);
}

bool HexDocument::nextModifiedOffset(std::uint64_t startOffset, bool forward, std::uint64_t& foundOffset) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return nextModifiedOffsetUnlocked(startOffset, forward, foundOffset);
}

bool HexDocument::isModifiedUnlocked(std::uint64_t offset) const {
    if (offset >= totalSize_) {
        return false;
    }
    std::uint64_t pos = 0;
    for (const auto& seg : segments_) {
        if (offset < pos + seg.length) {
            return seg.kind == Segment::Kind::Added;
        }
        pos += seg.length;
    }
    return false;
}

bool HexDocument::nextModifiedOffsetUnlocked(std::uint64_t startOffset,
                                             bool forward,
                                             std::uint64_t& foundOffset) const {
    foundOffset = 0;
    if (segments_.empty()) {
        return false;
    }
    if (!forward && startOffset > 0) {
        startOffset -= 1;
    }
    std::uint64_t pos = 0;
    if (forward) {
        for (const auto& seg : segments_) {
            const std::uint64_t segEnd = pos + seg.length;
            if (segEnd <= startOffset) {
                pos = segEnd;
                continue;
            }
            if (seg.kind == Segment::Kind::Added) {
                const std::uint64_t localStart = startOffset > pos ? startOffset : pos;
                foundOffset = localStart;
                return true;
            }
            pos = segEnd;
        }
        return false;
    }

    // backwards
    for (auto it = segments_.rbegin(); it != segments_.rend(); ++it) {
        const auto& seg = *it;
        const std::uint64_t segStart = (pos == 0) ? 0 : pos - seg.length;
        if (startOffset < segStart) {
            pos = segStart;
            continue;
        }
        if (seg.kind == Segment::Kind::Added && startOffset >= segStart) {
            foundOffset = startOffset;
            return true;
        }
        pos = segStart;
        if (pos == 0) {
            break;
        }
    }
    return false;
}

bool HexDocument::streamLogicalToFd(int fd, QString& errorOut) const {
    char buffer[64 * 1024];
    for (const auto& seg : segments_) {
        std::uint64_t remaining = seg.length;
        std::uint64_t offset = seg.sourceOffset;

        while (remaining > 0) {
            const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, sizeof(buffer)));
            QByteArray data;
            if (seg.kind == Segment::Kind::Original) {
                if (!readOriginalUnlocked(offset, chunk, data, errorOut)) {
                    return false;
                }
            }
            else {
                data = QByteArray(addedBuffer_.constData() + static_cast<int>(offset), static_cast<int>(chunk));
            }

            if (!writeAll(fd, data.constData(), static_cast<std::size_t>(data.size()), errorOut)) {
                return false;
            }
            remaining -= chunk;
            offset += chunk;
        }
    }
    return true;
}

bool HexDocument::writeTempFile(const QString& destPath, QString& errorOut, QString& tempPathOut) const {
    QFileInfo info(destPath);
    const QString dir = info.absolutePath().isEmpty() ? QStringLiteral(".") : info.absolutePath();
    QString tmpl = dir;
    if (!tmpl.endsWith(QLatin1Char('/'))) {
        tmpl += QLatin1Char('/');
    }
    tmpl += info.fileName().isEmpty() ? QStringLiteral("hexedit") : info.fileName();
    tmpl += QStringLiteral(".XXXXXX");

    QByteArray nativeTemplate = QFile::encodeName(tmpl);
    std::vector<char> pathBuf(nativeTemplate.begin(), nativeTemplate.end());
    pathBuf.push_back('\0');

    int tmpFd = ::mkstemp(pathBuf.data());
    if (tmpFd < 0) {
        errorOut = errnoString("mkstemp");
        return false;
    }

    bool ok = streamLogicalToFd(tmpFd, errorOut);
    if (ok) {
        if (::fsync(tmpFd) < 0) {
            ok = false;
            errorOut = errnoString("fsync");
        }
    }

    if (::close(tmpFd) < 0 && ok) {
        ok = false;
        errorOut = errnoString("close");
    }

    if (!ok) {
        ::unlink(pathBuf.data());
        return false;
    }

    tempPathOut = QString::fromLocal8Bit(pathBuf.data());
    return true;
}

bool HexDocument::detectExternalChange(QString& errorOut) const {
    FileStat current;
    if (!loadStat(path_, current, errorOut)) {
        return false;
    }

    if (current.dev != initialStat_.dev || current.ino != initialStat_.ino || current.size != initialStat_.size ||
        current.mtimeSec != initialStat_.mtimeSec || current.mtimeNsec != initialStat_.mtimeNsec) {
        errorOut = tr("The file has changed on disk since it was opened.");
        return false;
    }
    return true;
}

bool HexDocument::save(QString& errorOut, bool ignoreExternalChange) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const bool ok = saveInternal(errorOut, ignoreExternalChange);
    lock.unlock();
    if (ok) {
        Q_EMIT saved();
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::saveAs(const QString& newPath, QString& errorOut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const QString oldPath = path_;
    path_ = newPath;
    const bool ok = saveInternal(errorOut, /*ignoreExternalChange=*/true);
    if (!ok) {
        path_ = oldPath;
    }
    lock.unlock();
    if (ok) {
        Q_EMIT saved();
        Q_EMIT changed();
    }
    return ok;
}

bool HexDocument::saveInternal(QString& errorOut, bool ignoreExternalChange) {
    if (path_.isEmpty()) {
        errorOut = tr("No file is currently open.");
        return false;
    }

    if (!dirty_) {
        return true;
    }

    if (!isRegular_) {
        errorOut = tr("The file is not a regular file and cannot be saved safely.");
        return false;
    }

    if (!ignoreExternalChange) {
        QString changeError;
        if (!detectExternalChange(changeError)) {
            errorOut = changeError;
            return false;
        }
    }

    QString tempPath;
    if (!writeTempFile(path_, errorOut, tempPath)) {
        return false;
    }

    // Apply metadata
    int tmpFd = -1;
    if (!openDescriptor(tempPath, O_RDONLY | O_CLOEXEC, tmpFd, errorOut)) {
        ::unlink(QFile::encodeName(tempPath).constData());
        return false;
    }

    bool ok = true;
    if (::fchmod(tmpFd, initialStat_.mode & 07777) < 0) {
        errorOut = errnoString("fchmod");
        ok = false;
    }
    if (ok && ::fchown(tmpFd, initialStat_.uid, initialStat_.gid) < 0) {
        // not fatal on permission failure; just warn
    }
    struct timespec times[2];
    times[0].tv_sec = initialStat_.mtimeSec;
    times[0].tv_nsec = initialStat_.mtimeNsec;
    times[1] = times[0];
    if (ok && ::futimens(tmpFd, times) < 0) {
        // best effort
    }
    ::close(tmpFd);

    const QByteArray nativeTemp = QFile::encodeName(tempPath);
    const QByteArray nativeDest = QFile::encodeName(path_);
    if (ok && ::rename(nativeTemp.constData(), nativeDest.constData()) < 0) {
        errorOut = errnoString("rename");
        ::unlink(nativeTemp.constData());
        return false;
    }

    // Rebuild state against the new file contents
    if (!rebuildFromCurrentFile(errorOut)) {
        return false;
    }

    Q_EMIT saved();
    Q_EMIT changed();
    return true;
}

bool HexDocument::rebuildFromCurrentFile(QString& errorOut) {
    closeDescriptor(sourceFd_);
    segments_.clear();
    addedBuffer_.clear();
    undoStack_.clear();
    redoStack_.clear();
    reader_.reset();

    FileStat st;
    if (!loadStat(path_, st, errorOut)) {
        return false;
    }

    int fd = -1;
    if (!openDescriptor(path_, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, fd, errorOut)) {
        return false;
    }

    std::string readerErr;
    reader_ = std::make_unique<WindowedFileReader>(path_.toStdString(), kReadWindowSize, &readerErr);
    if (!reader_ || !reader_->valid()) {
        errorOut = QString::fromLocal8Bit(readerErr.c_str());
        return false;
    }

    sourceFd_ = fd;
    initialStat_ = st;
    currentStat_ = st;
    totalSize_ = st.size;

    if (totalSize_ > 0) {
        Segment seg;
        seg.kind = Segment::Kind::Original;
        seg.sourceOffset = 0;
        seg.length = totalSize_;
        segments_.push_back(seg);
    }

    dirty_ = false;
    return true;
}

}  // namespace Oneg4FM
