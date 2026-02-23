/*
 * Custom tab bar implementation for oneg4fm
 * oneg4fm/tabbar.cpp
 */

#include "tabbar.h"

// Qt Headers
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <algorithm>

namespace Oneg4FM {

namespace {
// Use QStringLiteral for MIME type to avoid runtime allocation
const QString kTabMimeType = QStringLiteral("application/oneg4fm-tab");
}  // namespace

const char* TabBar::tabDropped = "_oneg4fm_tab_dropped";

TabBar::TabBar(QWidget* parent) : QTabBar(parent), dragStarted_(false), detachable_(true) {
    // elide long tab titles on the right, in combination with minimumTabSizeHint()
    setElideMode(Qt::ElideRight);
}

void TabBar::mousePressEvent(QMouseEvent* event) {
    QTabBar::mousePressEvent(event);

    if (!detachable_) {
        return;
    }

    if (event->button() == Qt::LeftButton && tabAt(event->pos()) > -1) {
        dragStartPosition_ = event->pos();
    }
    else {
        dragStartPosition_ = QPoint();
    }

    dragStarted_ = false;
}

void TabBar::mouseMoveEvent(QMouseEvent* event) {
    if (!detachable_) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    // see if the drag threshold is exceeded and mark that a drag has started
    if (!dragStarted_ && !dragStartPosition_.isNull() &&
        (event->pos() - dragStartPosition_).manhattanLength() >= QApplication::startDragDistance()) {
        dragStarted_ = true;
    }

    // Check if dragging OUT of the window geometry
    if ((event->buttons() & Qt::LeftButton) && dragStarted_ &&
        !window()->geometry().contains(event->globalPosition().toPoint())) {
        handleTabDrag(event);
    }
    else {
        QTabBar::mouseMoveEvent(event);
    }
}

void TabBar::handleTabDrag(QMouseEvent* event) {
    if (currentIndex() == -1) {
        return;
    }

    // To be safe on Wayland and X11, the tab is only detached or dropped
    // *after* the drag operation finishes.
    // See MainWindow::dropEvent and the queued connection to TabBar::tabDetached

    QPointer<QDrag> drag = new QDrag(this);
    auto* mimeData = new QMimeData;
    mimeData->setData(kTabMimeType, QByteArray());
    drag->setMimeData(mimeData);

    const int tabCountBefore = count();
    const Qt::DropAction result = drag->exec(Qt::MoveAction);

    if (result != Qt::MoveAction) {
        // No oneg4fm window accepted the drop
        // Detach the tab if more than one tab is present, otherwise cancel cleanly
        if (tabCountBefore > 1) {
            Q_EMIT tabDetached();
        }
        else {
            finishMouseMoveEvent();
        }
    }
    else {
        // Another window may have accepted this drop
        // MainWindow::dropEvent sets the tabDropped property when we drop into ourselves
        const bool droppedIntoTabBar = property(tabDropped).toBool();
        if (droppedIntoTabBar) {
            setProperty(tabDropped, QVariant());
        }
        else {
            if (tabCountBefore > 1) {
                Q_EMIT tabDetached();
            }
            else {
                finishMouseMoveEvent();
            }
        }
    }

    event->accept();
    if (drag) {
        drag->deleteLater();
    }
}

void TabBar::finishMouseMoveEvent() {
    // Synthesize a neutral mouse move to reset internal drag state in QTabBar
    QMouseEvent finishingEvent(QEvent::MouseMove, QPoint(), QCursor::pos(), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QTabBar::mouseMoveEvent(&finishingEvent);
}

void TabBar::releaseMouse() {
    // Synthesize a release event to clean up pressed state when we cancel a drag
    QMouseEvent releasingEvent(QEvent::MouseButtonRelease, QPoint(), QCursor::pos(), Qt::LeftButton, Qt::NoButton,
                               Qt::NoModifier);
    QTabBar::mouseReleaseEvent(&releasingEvent);
}

void TabBar::mouseReleaseEvent(QMouseEvent* event) {
    if (detachable_) {
        // reset drag tracking
        dragStarted_ = false;
        dragStartPosition_ = QPoint();
    }

    // middle-click closes the tab under the cursor
    if (event->button() == Qt::MiddleButton) {
        const int index = tabAt(event->pos());
        if (index != -1) {
            Q_EMIT tabCloseRequested(index);
        }
    }

    QTabBar::mouseReleaseEvent(event);
}

// Let the main window receive dragged tabs
void TabBar::dragEnterEvent(QDragEnterEvent* event) {
    if (detachable_ && event->mimeData()->hasFormat(kTabMimeType)) {
        // ignore here so the main window can handle the drop
        event->ignore();
    }
}

// Limit the size of large tabs to 2/3 of the tabbar width or height
QSize TabBar::tabSizeHint(int index) const {
    const QSize base = QTabBar::tabSizeHint(index);

    switch (shape()) {
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast: {
            const int maxHeight = 2 * height() / 3;
            return QSize(base.width(), std::min(maxHeight, base.height()));
        }
        default: {
            const int maxWidth = 2 * width() / 3;
            return QSize(std::min(maxWidth, base.width()), base.height());
        }
    }
}

// Keep minimum tab size equal to the hint to avoid shrinking under eliding
QSize TabBar::minimumTabSizeHint(int index) const {
    return tabSizeHint(index);
}

void TabBar::tabInserted(int index) {
    QTabBar::tabInserted(index);

    // Qt6 sometimes fails to show the tab bar when the first tab is inserted
    // Updating the geometry after inserting the first non-auto-hidden tab works around this
    if (!autoHide() && index == 0 && count() == 1) {
        updateGeometry();
    }
}

}  // namespace Oneg4FM
