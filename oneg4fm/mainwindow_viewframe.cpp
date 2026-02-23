/*
 * Main window view frame implementation
 * oneg4fm/mainwindow_viewframe.cpp
 */

#include <QVBoxLayout>

#include "mainwindow.h"

namespace Oneg4FM {

ViewFrame::ViewFrame(QWidget* parent) : QFrame(parent), topBar_(nullptr) {
    QVBoxLayout* vBox = new QVBoxLayout;
    vBox->setContentsMargins(0, 0, 0, 0);

    tabBar_ = new TabBar;
    tabBar_->setFocusPolicy(Qt::NoFocus);
    stackedWidget_ = new QStackedWidget;
    vBox->addWidget(tabBar_);
    vBox->addWidget(stackedWidget_, 1);
    setLayout(vBox);

    // tabbed browsing interface
    tabBar_->setDocumentMode(true);
    tabBar_->setExpanding(false);
    tabBar_->setMovable(true);  // reorder the tabs by dragging
    // switch to the tab under the cursor during dnd.
    tabBar_->setChangeCurrentOnDrag(true);
    tabBar_->setAcceptDrops(true);
    tabBar_->setContextMenuPolicy(Qt::CustomContextMenu);
}

void ViewFrame::createTopBar(bool usePathButtons) {
    if (QVBoxLayout* vBox = qobject_cast<QVBoxLayout*>(layout())) {
        if (usePathButtons) {
            if (qobject_cast<Panel::PathEdit*>(topBar_)) {
                delete topBar_;
                topBar_ = nullptr;
            }
            if (topBar_ == nullptr) {
                topBar_ = new Panel::PathBar();
                vBox->insertWidget(0, topBar_);
            }
        }
        else {
            if (qobject_cast<Panel::PathBar*>(topBar_)) {
                delete topBar_;
                topBar_ = nullptr;
            }
            if (topBar_ == nullptr) {
                topBar_ = new Panel::PathEdit();
                vBox->insertWidget(0, topBar_);
            }
        }
    }
}

void ViewFrame::removeTopBar() {
    if (topBar_ != nullptr) {
        if (QVBoxLayout* vBox = qobject_cast<QVBoxLayout*>(layout())) {
            vBox->removeWidget(topBar_);
            delete topBar_;
            topBar_ = nullptr;
        }
    }
}

}  // namespace Oneg4FM