/*
 * Simple ImageMagick-backed image viewer
 * oneg4fm/image_viewer_window.h
 */

#pragma once

#include <QMainWindow>
#include <QPixmap>
#include <QString>
#include <QLabel>

namespace Oneg4FM {

class ImageViewerWindow : public QMainWindow {
    Q_OBJECT
   public:
    explicit ImageViewerWindow(const QString& path, QWidget* parent = nullptr);
    ~ImageViewerWindow() override = default;

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private Q_SLOTS:
    void toggleFullScreen();

   private:
    bool loadImage(const QString& path);
    void updateScaledPixmap();

   private:
    QString path_;
    QPixmap original_;
    QPixmap scaled_;
    QLabel* label_ = nullptr;
    double fitRatio_ = 0.6;
};

}  // namespace Oneg4FM
