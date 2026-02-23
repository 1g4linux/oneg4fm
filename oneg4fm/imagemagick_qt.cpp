/*
 * Qt helpers for ImageMagick thumbnails and previews
 * oneg4fm/imagemagick_qt.cpp
 */

#include "imagemagick_qt.h"

#include <QImage>
#include <QLoggingCategory>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QString>
#include <algorithm>
#include <cstring>

namespace Oneg4FM {

namespace {

QLoggingCategory kMagickLog("oneg4fm.imagemagick");
constexpr int kMaxRequestedMagickDimension = 16384;

bool bufferMatchesSize(const ImageMagickBuffer& buf) {
    const qsizetype expected = static_cast<qsizetype>(buf.width) * static_cast<qsizetype>(buf.height) * 4;
    return expected > 0 && expected <= static_cast<qsizetype>(buf.pixels.size());
}

QSize sanitizedDecodeSize(const QSize& requested) {
    const int width = std::clamp(requested.width(), 1, kMaxRequestedMagickDimension);
    const int height = std::clamp(requested.height(), 1, kMaxRequestedMagickDimension);
    return QSize(width, height);
}

struct ThumbnailResult {
    QString key;
    quint64 mtime = 0;
    QImage image;
};

ThumbnailResult runThumbnailJob(const QString& key, const QString& path, quint64 mtime, int size) {
    ThumbnailResult result;
    result.key = key;
    result.mtime = mtime;
    const QSize thumbSize(size, size);
    result.image = createThumbnailImageMagick(path, thumbSize);
    return result;
}

}  // namespace

QPixmap pixmapFromMagickBuffer(const ImageMagickBuffer& buf) {
    if (buf.width <= 0 || buf.height <= 0) {
        return {};
    }

    if (!bufferMatchesSize(buf)) {
        qCWarning(kMagickLog, "Magick buffer size mismatch (%d x %d)", buf.width, buf.height);
        return {};
    }

    QImage img(buf.width, buf.height, QImage::Format_RGBA8888);
    if (img.isNull()) {
        return {};
    }

    const qsizetype stride = static_cast<qsizetype>(buf.width) * 4;
    for (int y = 0; y < buf.height; ++y) {
        std::memcpy(img.scanLine(y), buf.pixels.data() + static_cast<qsizetype>(y) * stride,
                    static_cast<size_t>(stride));
    }

    return QPixmap::fromImage(std::move(img));
}

QImage createThumbnailImageMagick(const QString& path, const QSize& thumbSize) {
    if (!ImageMagickSupport::isAvailable()) {
        return {};
    }

    const QSize boundedThumb = sanitizedDecodeSize(thumbSize);
    ImageMagickBuffer buffer;
    if (!ImageMagickSupport::loadThumbnailBuffer(path, boundedThumb.width(), boundedThumb.height(), buffer)) {
        return {};
    }

    if (!bufferMatchesSize(buffer)) {
        qCWarning(kMagickLog, "Magick buffer size mismatch (%d x %d)", buffer.width, buffer.height);
        return {};
    }

    QImage img(buffer.width, buffer.height, QImage::Format_RGBA8888);
    if (img.isNull()) {
        return {};
    }

    const qsizetype stride = static_cast<qsizetype>(buffer.width) * 4;
    for (int y = 0; y < buffer.height; ++y) {
        std::memcpy(img.scanLine(y), buffer.pixels.data() + static_cast<qsizetype>(y) * stride,
                    static_cast<size_t>(stride));
    }

    return img;
}

QPixmap createThumbnailPixmapMagick(const QString& path, const QSize& thumbSize) {
    QImage img = createThumbnailImageMagick(path, thumbSize);
    if (img.isNull()) {
        return {};
    }

    return QPixmap::fromImage(std::move(img));
}

QPixmap createPreviewPixmapMagick(const QString& path, const QSize& maxSize) {
    if (!ImageMagickSupport::isAvailable()) {
        return {};
    }

    const QSize boundedSize = sanitizedDecodeSize(maxSize);
    ImageMagickBuffer buffer;
    if (!ImageMagickSupport::loadPreviewBuffer(path, boundedSize.width(), boundedSize.height(), buffer)) {
        return {};
    }

    return pixmapFromMagickBuffer(buffer);
}

QPixmap createImagePixmapMagick(const QString& path) {
    if (!ImageMagickSupport::isAvailable()) {
        return {};
    }

    ImageMagickBuffer buffer;
    if (!ImageMagickSupport::loadImageBuffer(path, buffer)) {
        return {};
    }

    return pixmapFromMagickBuffer(buffer);
}

ImageMagickProxyFolderModel::ImageMagickProxyFolderModel(QObject* parent)
    : Panel::ProxyFolderModel(parent), magickThumbnailsEnabled_(false), magickThumbnailSize_(0) {
    thumbnailCache_.setMaxCost(256);
    const int ideal = QThread::idealThreadCount();
    const int threads = std::clamp(ideal > 0 ? ideal / 2 : 2, 2, 4);
    thumbnailPool_.setMaxThreadCount(threads);
    thumbnailPool_.setExpiryTimeout(15000);
}

void ImageMagickProxyFolderModel::setShowThumbnails(bool show) {
    magickThumbnailsEnabled_ = show;
    thumbnailCache_.clear();
    pendingThumbnails_.clear();
    inFlightThumbnails_.clear();
    Panel::ProxyFolderModel::setShowThumbnails(show);
}

void ImageMagickProxyFolderModel::setThumbnailSize(int size) {
    magickThumbnailSize_ = size;
    thumbnailCache_.clear();
    pendingThumbnails_.clear();
    inFlightThumbnails_.clear();
    Panel::ProxyFolderModel::setThumbnailSize(size);
}

QVariant ImageMagickProxyFolderModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::DecorationRole && index.column() == 0 && magickThumbnailsEnabled_ &&
        ImageMagickSupport::isAvailable()) {
        auto info = fileInfoFromIndex(index);
        if (info && info->isNative() && isImageFile(info)) {
            const QString path = pathForFile(info);
            const int size = magickThumbnailSize_ > 0 ? magickThumbnailSize_ : 128;
            const quint64 mtime = info->mtime();
            if (!path.isEmpty() && size > 0) {
                const QString key = cacheKey(path, size, mtime);
                if (auto* cached = thumbnailCache_.object(key)) {
                    return QIcon(*cached);
                }

                requestThumbnailForIndex(index, info);
            }
        }
    }

    return Panel::ProxyFolderModel::data(index, role);
}

void ImageMagickProxyFolderModel::prefetchThumbnails(const QModelIndexList& indexes) const {
    if (!magickThumbnailsEnabled_ || !ImageMagickSupport::isAvailable()) {
        return;
    }

    for (const QModelIndex& idx : indexes) {
        auto info = fileInfoFromIndex(idx);
        if (info && info->isNative() && isImageFile(info)) {
            requestThumbnailForIndex(idx, info);
        }
    }
}

bool ImageMagickProxyFolderModel::isImageFile(const std::shared_ptr<const Panel::FileInfo>& info) const {
    return info && info->isImage() && !info->isSymlink() && info->canThumbnail();
}

QString ImageMagickProxyFolderModel::pathForFile(const std::shared_ptr<const Panel::FileInfo>& info) const {
    if (!info || !info->isNative()) {
        return {};
    }
    auto localPath = info->path().localPath();
    if (!localPath) {
        return {};
    }
    return QString::fromUtf8(localPath.get());
}

QString ImageMagickProxyFolderModel::cacheKey(const QString& path, int size, quint64 mtime) const {
    return path + QLatin1Char('|') + QString::number(size) + QLatin1Char('|') + QString::number(mtime);
}

void ImageMagickProxyFolderModel::requestThumbnailForIndex(const QModelIndex& index,
                                                           const std::shared_ptr<const Panel::FileInfo>& info) const {
    if (!index.isValid() || !info) {
        return;
    }

    const QString path = pathForFile(info);
    const int size = magickThumbnailSize_ > 0 ? magickThumbnailSize_ : 128;
    const quint64 mtime = info->mtime();
    if (path.isEmpty() || size <= 0) {
        return;
    }

    const QString key = cacheKey(path, size, mtime);
    if (auto* cached = thumbnailCache_.object(key)) {
        Q_UNUSED(cached);
        pendingThumbnails_.remove(key);
        inFlightThumbnails_.remove(key);
        return;
    }

    QPersistentModelIndex persistent(index);
    auto& pendingIndexes = pendingThumbnails_[key];
    if (!pendingIndexes.contains(persistent)) {
        pendingIndexes.push_back(persistent);
    }

    if (inFlightThumbnails_.contains(key)) {
        return;
    }

    inFlightThumbnails_.insert(key);
    auto* self = const_cast<ImageMagickProxyFolderModel*>(this);
    auto* watcher = new QFutureWatcher<ThumbnailResult>(self);
    connect(watcher, &QFutureWatcherBase::finished, self, [self, watcher] {
        ThumbnailResult result = watcher->result();
        watcher->deleteLater();

        self->inFlightThumbnails_.remove(result.key);
        const auto indexes = self->pendingThumbnails_.take(result.key);

        if (result.image.isNull()) {
            return;
        }

        QPixmap pix = QPixmap::fromImage(result.image);
        if (pix.isNull()) {
            return;
        }

        self->thumbnailCache_.insert(result.key, new QPixmap(pix), 1);
        for (const auto& idx : indexes) {
            if (idx.isValid()) {
                Q_EMIT self->dataChanged(idx, idx, {Qt::DecorationRole});
            }
        }
    });

    watcher->setFuture(QtConcurrent::run(&thumbnailPool_,
                                         [key, path, mtime, size] { return runThumbnailJob(key, path, mtime, size); }));
}

}  // namespace Oneg4FM
