/*
 * Main window drag and drop implementation
 * oneg4fm/mainwindow_dragdrop.cpp
 */

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPointer>
#include <QTimer>

#include "application.h"
#include "mainwindow.h"
#include "tabbar.h"
#include "tabpage.h"

namespace Oneg4FM {

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event) {
        return;
    }

    const QMimeData* mime = event->mimeData();
    if (!mime || !mime->hasFormat(QStringLiteral("application/oneg4fm-tab"))) {
        return;
    }

    // ensure that the tab drag source is ours (and not a root window, for example)
    if (event->source()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event) {
        return;
    }

    const QMimeData* mime = event->mimeData();
    if (mime && mime->hasFormat(QStringLiteral("application/oneg4fm-tab"))) {
        if (QObject* sourceObject = event->source()) {
            // announce that the tab drop is accepted by us (see TabBar::mouseMoveEvent)
            sourceObject->setProperty(TabBar::tabDropped, true);

            // ensure the source object is still alive when the deferred drop is processed
            QPointer<QObject> safeSourceObject = sourceObject;
            QTimer::singleShot(0, safeSourceObject, [this, safeSourceObject]() {
                if (safeSourceObject) {
                    dropTab(safeSourceObject);
                }
            });
        }
    }

    event->acceptProposedAction();
}

void MainWindow::dropTab(QObject* source) {
    if (!activeViewFrame_) {
        return;
    }

    auto* widget = qobject_cast<QWidget*>(source);
    if (!widget) {
        activeViewFrame_->getTabBar()->finishMouseMoveEvent();
        return;
    }

    auto* dragSource = qobject_cast<MainWindow*>(widget->window());
    if (dragSource == this || !dragSource) {
        activeViewFrame_->getTabBar()->finishMouseMoveEvent();
        return;
    }

    // first close the tab in the drag window; then add its page to a new tab in the drop window
    TabPage* dropPage = dragSource->currentPage();
    if (dropPage) {
        QObject::disconnect(dropPage, nullptr, dragSource, nullptr);

        // release mouse before tab removal because otherwise, the source tabbar
        // might not be updated properly with tab reordering during a fast drag-and-drop
        dragSource->activeViewFrame_->getTabBar()->releaseMouse();

        auto* pageWidget = static_cast<QWidget*>(dropPage);
        dragSource->activeViewFrame_->getStackedWidget()->removeWidget(pageWidget);

        const int index = addTabWithPage(dropPage, activeViewFrame_);
        activeViewFrame_->getTabBar()->setCurrentIndex(index);
    }
    else {
        activeViewFrame_->getTabBar()->finishMouseMoveEvent();  // impossible
    }
}

void MainWindow::detachTab() {
    if (!activeViewFrame_) {
        return;
    }

    auto* app = qobject_cast<Application*>(qApp);
    const bool splitViewEnabled = app && app->settings().splitView();

    // don't detach a single tab; split view state may have changed elsewhere
    if (activeViewFrame_->getStackedWidget()->count() == 1 || splitViewEnabled) {
        activeViewFrame_->getTabBar()->finishMouseMoveEvent();
        return;
    }

    // close the tab and move its page to a new window
    TabPage* dropPage = currentPage();
    if (dropPage) {
        QObject::disconnect(dropPage, nullptr, this, nullptr);

        activeViewFrame_->getTabBar()->releaseMouse();  // as in dropTab()
        auto* pageWidget = static_cast<QWidget*>(dropPage);
        activeViewFrame_->getStackedWidget()->removeWidget(pageWidget);

        auto* newWin = new MainWindow();
        newWin->addTabWithPage(dropPage, newWin->activeViewFrame_);
        newWin->show();
    }
    else {
        activeViewFrame_->getTabBar()->finishMouseMoveEvent();  // impossible
    }
}

}  // namespace Oneg4FM
