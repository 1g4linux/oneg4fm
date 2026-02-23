/*
 * Simple ImageMagick-backed image viewer
 * oneg4fm/image_viewer_window.cpp
 */

#include "image_viewer_window.h"

#include <QKeySequence>
#include <QLabel>
#include <QResizeEvent>
#include <QShortcut>
#include <QStatusBar>
#include <QVBoxLayout>

#include "imagemagick_qt.h"

namespace Oneg4FM {

ImageViewerWindow::ImageViewerWindow(const QString& path, QWidget* parent)
    : QMainWindow(parent), path_(path), label_(nullptr), fitRatio_(0.9) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(path_);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    label_ = new QLabel(this);
    label_->setAlignment(Qt::AlignCenter);
    label_->setBackgroundRole(QPalette::Base);
    label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(label_);

    setCentralWidget(central);
    statusBar()->setSizeGripEnabled(false);

    auto* shortcutFull = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(shortcutFull, &QShortcut::activated, this, &ImageViewerWindow::toggleFullScreen);
    auto* shortcutEsc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(shortcutEsc, &QShortcut::activated, this, [this] {
        if (isFullScreen()) {
            toggleFullScreen();
        }
    });

    if (!loadImage(path_)) {
        label_->setText(tr("Could not load image."));
        return;
    }

    updateScaledPixmap();
    resize(original_.size().boundedTo(QSize(1600, 1200)));
}

bool ImageViewerWindow::loadImage(const QString& path) {
    QPixmap pix = createImagePixmapMagick(path);
    if (pix.isNull()) {
        return false;
    }
    original_ = std::move(pix);
    return true;
}

void ImageViewerWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateScaledPixmap();
}

void ImageViewerWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
    }
    else {
        showFullScreen();
    }
    updateScaledPixmap();
}

void ImageViewerWindow::updateScaledPixmap() {
    if (original_.isNull() || !label_) {
        return;
    }

    const QSize area = centralWidget() ? centralWidget()->size() : size();
    const QSize target(static_cast<int>(area.width() * fitRatio_), static_cast<int>(area.height() * fitRatio_));

    scaled_ = original_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    label_->setPixmap(scaled_);

    if (!scaled_.isNull()) {
        const double zoom =
            (original_.width() > 0) ? (100.0 * scaled_.width() / static_cast<double>(original_.width())) : 0.0;
        statusBar()->showMessage(
            tr("%1 × %2  (%3%)").arg(scaled_.width()).arg(scaled_.height()).arg(static_cast<int>(zoom)));
    }
}

}  // namespace Oneg4FM
