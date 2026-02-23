/*
 * Qt-native Path Bar widget
 * src/ui/pathbar.cpp
 */

#include "pathbar.h"
#include "../panel/panel.h"  // Required for Panel::FilePath
#include <QDir>

namespace Oneg4FM {

PathBar::PathBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    pathEdit_ = new QLineEdit(this);
    connect(pathEdit_, &QLineEdit::returnPressed, this, &PathBar::onReturnPressed);
    connect(pathEdit_, &QLineEdit::editingFinished, this, &PathBar::editingFinished);

    layout->addWidget(pathEdit_);
}

PathBar::~PathBar() = default;

void PathBar::setPath(const QUrl& path) {
    currentPath_ = path;
    if (path.isLocalFile()) {
        pathEdit_->setText(path.toLocalFile());
    }
    else {
        pathEdit_->setText(path.toString());
    }
}

QUrl PathBar::path() const {
    return currentPath_;
}

void PathBar::setText(const QString& text) {
    setPath(QUrl::fromUserInput(text));
}

QString PathBar::text() const {
    return pathEdit_->text();
}

void PathBar::selectAll() {
    pathEdit_->selectAll();
}

void PathBar::openEditor() {
    pathEdit_->setFocus();
    pathEdit_->selectAll();
}

void PathBar::onReturnPressed() {
    Q_EMIT returnPressed();

    QString text = pathEdit_->text();
    QUrl url = QUrl::fromUserInput(text);

    if (url.isValid()) {
        Q_EMIT pathChanged(url);

        Panel::FilePath fp(url);
        Q_EMIT chdir(fp);
    }
}

}  // namespace Oneg4FM