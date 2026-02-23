/*
 * Qt helpers for ImageMagick thumbnails and previews
 * oneg4fm/imagemagick_qt.h
 */

#pragma once

#include <QCache>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QModelIndex>
#include <QPixmap>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSize>
#include <QThreadPool>
#include <memory>

#include "panel/panel.h"

#include "imagemagick_support.h"

namespace Oneg4FM {

QPixmap pixmapFromMagickBuffer(const ImageMagickBuffer& buf);
QPixmap createThumbnailPixmapMagick(const QString& path, const QSize& thumbSize);
QImage createThumbnailImageMagick(const QString& path, const QSize& thumbSize);
QPixmap createPreviewPixmapMagick(const QString& path, const QSize& maxSize);
QPixmap createImagePixmapMagick(const QString& path);

class ImageMagickProxyFolderModel : public Panel::ProxyFolderModel {
    Q_OBJECT
   public:
    explicit ImageMagickProxyFolderModel(QObject* parent = nullptr);

    void setShowThumbnails(bool show);
    void setThumbnailSize(int size);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void prefetchThumbnails(const QModelIndexList& indexes) const;

   private:
    bool isImageFile(const std::shared_ptr<const Panel::FileInfo>& info) const;
    QString pathForFile(const std::shared_ptr<const Panel::FileInfo>& info) const;
    QString cacheKey(const QString& path, int size, quint64 mtime) const;
    void requestThumbnailForIndex(const QModelIndex& index, const std::shared_ptr<const Panel::FileInfo>& info) const;

   private:
    mutable QCache<QString, QPixmap> thumbnailCache_;
    mutable QHash<QString, QList<QPersistentModelIndex>> pendingThumbnails_;
    mutable QSet<QString> inFlightThumbnails_;
    mutable QThreadPool thumbnailPool_;
    bool magickThumbnailsEnabled_;
    int magickThumbnailSize_;
};

}  // namespace Oneg4FM
