/*
 * ImageMagick backend utilities
 * oneg4fm/imagemagick_support.cpp
 */

#include "imagemagick_support.h"

#include <QFile>

#ifdef HAVE_MAGICKWAND
#include <MagickWand/MagickWand.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

#ifdef HAVE_MAGICKWAND

class MagickInitializer {
   public:
    MagickInitializer() {
        MagickWandGenesis();
        // Keep decode resources bounded so malformed or bomb images cannot
        // consume unbounded process memory/CPU time.
        MagickSetResourceLimit(MemoryResource, 256ULL * 1024ULL * 1024ULL);
        MagickSetResourceLimit(MapResource, 512ULL * 1024ULL * 1024ULL);
        MagickSetResourceLimit(DiskResource, 1024ULL * 1024ULL * 1024ULL);
        MagickSetResourceLimit(AreaResource, 64ULL * 1024ULL * 1024ULL);
        MagickSetResourceLimit(WidthResource, 16384);
        MagickSetResourceLimit(HeightResource, 16384);
        MagickSetResourceLimit(TimeResource, 30);
    }
    ~MagickInitializer() { MagickWandTerminus(); }
};

MagickInitializer g_magickInitializer;

constexpr qint64 kMaxImageInputBytes = 64LL * 1024LL * 1024LL;
constexpr size_t kMaxDecodedDimension = 16384;
constexpr size_t kMaxDecodedPixels = 64ULL * 1024ULL * 1024ULL;
constexpr size_t kRgbaChannels = 4;

bool imageInputStatIsSafe(const struct stat& st) {
    return S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= kMaxImageInputBytes;
}

bool statPathNoFollow(const QByteArray& encodedPath, struct stat& st) {
#if defined(AT_FDCWD) && defined(AT_SYMLINK_NOFOLLOW)
    return ::fstatat(AT_FDCWD, encodedPath.constData(), &st, AT_SYMLINK_NOFOLLOW) == 0;
#else
    return ::lstat(encodedPath.constData(), &st) == 0;
#endif
}

int safeImageOpenFlags() {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
    return flags;
}

bool multiplyChecked(size_t lhs, size_t rhs, size_t& out) {
    if (lhs == 0 || rhs == 0) {
        out = 0;
        return true;
    }
    if (lhs > std::numeric_limits<size_t>::max() / rhs) {
        return false;
    }
    out = lhs * rhs;
    return true;
}

bool dimensionsWithinLimits(size_t width, size_t height) {
    if (width == 0 || height == 0) {
        return false;
    }

    if (width > kMaxDecodedDimension || height > kMaxDecodedDimension) {
        return false;
    }

    size_t pixelCount = 0;
    if (!multiplyChecked(width, height, pixelCount)) {
        return false;
    }

    return pixelCount <= kMaxDecodedPixels;
}

bool computeRgbaBufferLayout(size_t width, size_t height, size_t& stride, size_t& bufferSize) {
    stride = 0;
    bufferSize = 0;

    if (!multiplyChecked(width, kRgbaChannels, stride)) {
        return false;
    }
    if (!multiplyChecked(stride, height, bufferSize)) {
        return false;
    }

    using ByteVector = std::vector<unsigned char>;
    const size_t maxSize = ByteVector().max_size();
    return bufferSize <= maxSize;
}

bool writeFilePosix(const QString& path, const unsigned char* data, size_t size) {
    const QByteArray encoded = QFile::encodeName(path);
    int fd = ::open(encoded.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        return false;
    }

    size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }

    // Best-effort durability: fsync before closing.
    ::fsync(fd);

    if (::close(fd) != 0) {
        return false;
    }

    return true;
}

bool loadWandFromFile(MagickWand* wand, const QString& path) {
    if (!wand) {
        return false;
    }

    const QByteArray encoded = QFile::encodeName(path);
    struct stat st{};
    if (!statPathNoFollow(encoded, st) || !imageInputStatIsSafe(st)) {
        return false;
    }

    int fd = ::open(encoded.constData(), safeImageOpenFlags());
    if (fd < 0) {
        return false;
    }

    if (::fstat(fd, &st) != 0 || !imageInputStatIsSafe(st)) {
        ::close(fd);
        return false;
    }

    FILE* stream = ::fdopen(fd, "rb");
    if (!stream) {
        ::close(fd);
        return false;
    }

    bool ok = false;
    MagickWand* probe = NewMagickWand();
    if (probe && MagickPingImageFile(probe, stream) != MagickFalse) {
        const size_t sourceWidth = MagickGetImageWidth(probe);
        const size_t sourceHeight = MagickGetImageHeight(probe);

        if (dimensionsWithinLimits(sourceWidth, sourceHeight) && ::fseek(stream, 0, SEEK_SET) == 0 &&
            MagickReadImageFile(wand, stream) != MagickFalse) {
            const size_t decodedWidth = MagickGetImageWidth(wand);
            const size_t decodedHeight = MagickGetImageHeight(wand);
            ok = dimensionsWithinLimits(decodedWidth, decodedHeight);
        }
    }

    if (probe) {
        DestroyMagickWand(probe);
    }
    ::fclose(stream);
    return ok;
}

bool saveWandToFile(MagickWand* wand, const QString& path) {
    size_t blobSize = 0;
    unsigned char* blob = MagickGetImageBlob(wand, &blobSize);
    if (!blob || blobSize == 0) {
        return false;
    }

    const bool ok = writeFilePosix(path, blob, blobSize);
    MagickRelinquishMemory(blob);
    return ok;
}

bool fillBufferFromWand(MagickWand* wand, ImageMagickBuffer& out) {
    const size_t w = MagickGetImageWidth(wand);
    const size_t h = MagickGetImageHeight(wand);

    if (!dimensionsWithinLimits(w, h) || w > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        h > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    if (MagickGetImageAlphaChannel(wand) == MagickFalse) {
        MagickSetImageAlphaChannel(wand, OpaqueAlphaChannel);
    }
    MagickSetImageType(wand, TrueColorAlphaType);

    size_t stride = 0;
    size_t bufferSize = 0;
    if (!computeRgbaBufferLayout(w, h, stride, bufferSize)) {
        return false;
    }

    ImageMagickBuffer buf;
    buf.width = static_cast<int>(w);
    buf.height = static_cast<int>(h);
    buf.pixels.resize(bufferSize);

    if (MagickExportImagePixels(wand, 0, 0, w, h, "RGBA", CharPixel, buf.pixels.data()) == MagickFalse) {
        return false;
    }

    out = std::move(buf);
    return true;
}

#endif  // HAVE_MAGICKWAND

}  // namespace

bool ImageMagickSupport::isAvailable() {
#ifdef HAVE_MAGICKWAND
    return true;
#else
    return false;
#endif
}

bool ImageMagickSupport::probe(const QString& path, ImageMagickInfo& outInfo) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    const size_t w = MagickGetImageWidth(wand);
    const size_t h = MagickGetImageHeight(wand);

    char* fmt = MagickGetImageFormat(wand);
    const ColorspaceType cs = MagickGetImageColorspace(wand);

    outInfo.width = static_cast<quint64>(w);
    outInfo.height = static_cast<quint64>(h);
    if (fmt) {
        outInfo.format = QString::fromLatin1(fmt);
        MagickRelinquishMemory(fmt);
    }

    switch (cs) {
        case sRGBColorspace:
            outInfo.colorSpace = QStringLiteral("sRGB");
            break;
        case CMYKColorspace:
            outInfo.colorSpace = QStringLiteral("CMYK");
            break;
        default:
            outInfo.colorSpace = QStringLiteral("Other");
            break;
    }

    outInfo.hasAlpha = MagickGetImageAlphaChannel(wand) == MagickTrue;

    DestroyMagickWand(wand);
    return true;
#else
    Q_UNUSED(path)
    Q_UNUSED(outInfo)
    return false;
#endif
}

bool ImageMagickSupport::loadThumbnailBuffer(const QString& path, int maxWidth, int maxHeight, ImageMagickBuffer& out) {
#ifdef HAVE_MAGICKWAND
    if (maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    if (MagickThumbnailImage(wand, static_cast<size_t>(maxWidth), static_cast<size_t>(maxHeight)) == MagickFalse) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = fillBufferFromWand(wand, out);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(path)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(out)
    return false;
#endif
}

bool ImageMagickSupport::loadPreviewBuffer(const QString& path, int maxWidth, int maxHeight, ImageMagickBuffer& out) {
#ifdef HAVE_MAGICKWAND
    if (maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    if (MagickResizeImage(wand, static_cast<size_t>(maxWidth), static_cast<size_t>(maxHeight), LanczosFilter) ==
        MagickFalse) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = fillBufferFromWand(wand, out);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(path)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(out)
    return false;
#endif
}

bool ImageMagickSupport::loadImageBuffer(const QString& path, ImageMagickBuffer& out) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = fillBufferFromWand(wand, out);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(path)
    Q_UNUSED(out)
    return false;
#endif
}

bool ImageMagickSupport::convertFormat(const QString& srcPath, const QString& dstPath, const QByteArray& format) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand);
        return false;
    }

    if (!format.isEmpty()) {
        if (MagickSetImageFormat(wand, format.constData()) == MagickFalse) {
            DestroyMagickWand(wand);
            return false;
        }
    }

    const bool ok = saveWandToFile(wand, dstPath);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(format)
    return false;
#endif
}

bool ImageMagickSupport::resizeImage(const QString& srcPath,
                                     const QString& dstPath,
                                     int targetWidth,
                                     int targetHeight,
                                     bool keepAspect) {
#ifdef HAVE_MAGICKWAND
    if (targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand);
        return false;
    }

    const size_t w = MagickGetImageWidth(wand);
    const size_t h = MagickGetImageHeight(wand);
    if (w == 0 || h == 0) {
        DestroyMagickWand(wand);
        return false;
    }

    size_t newW = static_cast<size_t>(targetWidth);
    size_t newH = static_cast<size_t>(targetHeight);

    if (keepAspect) {
        const double aspectSrc = static_cast<double>(w) / static_cast<double>(h);
        const double aspectTarget = static_cast<double>(targetWidth) / static_cast<double>(targetHeight);

        if (aspectSrc > aspectTarget) {
            newH = static_cast<size_t>(static_cast<double>(newW) / aspectSrc);
        }
        else {
            newW = static_cast<size_t>(static_cast<double>(newH) * aspectSrc);
        }
    }

    if (MagickResizeImage(wand, newW, newH, LanczosFilter) == MagickFalse) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = saveWandToFile(wand, dstPath);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(targetWidth)
    Q_UNUSED(targetHeight)
    Q_UNUSED(keepAspect)
    return false;
#endif
}

bool ImageMagickSupport::rotateImage(const QString& srcPath, const QString& dstPath, double degrees) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand);
        return false;
    }

    PixelWand* bg = NewPixelWand();
    PixelSetColor(bg, "none");

    if (MagickRotateImage(wand, bg, degrees) == MagickFalse) {
        DestroyPixelWand(bg);
        DestroyMagickWand(wand);
        return false;
    }

    DestroyPixelWand(bg);

    const bool ok = saveWandToFile(wand, dstPath);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(degrees)
    return false;
#endif
}
