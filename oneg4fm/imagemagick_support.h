/*
 * ImageMagick backend utilities
 * oneg4fm/imagemagick_support.h
 */

#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <QString>
#include <vector>

struct ImageMagickBuffer {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;  // RGBA, row-major, width * height * 4
};

struct ImageMagickInfo {
    quint64 width = 0;
    quint64 height = 0;
    QString format;
    QString colorSpace;
    bool hasAlpha = false;
};

class ImageMagickSupport {
   public:
    static bool isAvailable();

    static bool probe(const QString& path, ImageMagickInfo& outInfo);

    static bool loadThumbnailBuffer(const QString& path, int maxWidth, int maxHeight, ImageMagickBuffer& out);

    static bool loadPreviewBuffer(const QString& path, int maxWidth, int maxHeight, ImageMagickBuffer& out);

    static bool loadImageBuffer(const QString& path, ImageMagickBuffer& out);

    static bool convertFormat(const QString& srcPath, const QString& dstPath, const QByteArray& format);

    static bool resizeImage(const QString& srcPath,
                            const QString& dstPath,
                            int targetWidth,
                            int targetHeight,
                            bool keepAspect = true);

    static bool rotateImage(const QString& srcPath, const QString& dstPath, double degrees);
};
