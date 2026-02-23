/*
 * Custom tab bar header
 * oneg4fm/tabbar.h
 */

#ifndef FM_TABBAR_H
#define FM_TABBAR_H

#include <QTabBar>

class QMouseEvent;

namespace Oneg4FM {

class TabBar : public QTabBar {
    Q_OBJECT

   public:
    explicit TabBar(QWidget* parent = nullptr);
    void finishMouseMoveEvent();
    void releaseMouse();

    void setDetachable(bool detachable) {
        detachable_ = detachable;
        // also, reset drag info
        dragStarted_ = false;
        dragStartPosition_ = QPoint();
    }

    // An object property used for knowing whether
    // a tab is dropped into one of our windows:
    static const char* tabDropped;

   Q_SIGNALS:
    void tabDetached();

   protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void tabInserted(int index) override;

   private:
    void handleTabDrag(QMouseEvent* event);

    QPoint dragStartPosition_;
    bool dragStarted_;
    bool detachable_;
};

}  // namespace Oneg4FM

#endif  // FM_TABBAR_H
