/*
 * Status bar header
 * oneg4fm/statusbar.h
 */

#ifndef FM_STATUSBAR_H
#define FM_STATUSBAR_H

#include <QLabel>
#include <QStatusBar>
#include <QTimer>

namespace Oneg4FM {

class Label : public QLabel {
    Q_OBJECT

   public:
    explicit Label(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QString elidedText_;
    QString lastText_;
    int lastWidth_;
};

class StatusBar : public QStatusBar {
    Q_OBJECT

   public:
    explicit StatusBar(QWidget* parent = nullptr);
    ~StatusBar();

   public Q_SLOTS:
    void showMessage(const QString& message, int timeout = 0);

   protected Q_SLOTS:
    void reallyShowMessage();

   private:
    Label* statusLabel_;  // for a stable (elided) text
    QTimer* messageTimer_;
    QString lastMessage_;
    int lastTimeOut_;
};

}  // namespace Oneg4FM

#endif  // FM_STATUSBAR_H
