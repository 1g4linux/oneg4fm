/*
 * Status bar implementation for oneg4fm
 * oneg4fm/statusbar.cpp
 */

#include "statusbar.h"

#include <QPainter>
#include <QStyleOption>
#include <QTimer>

namespace Oneg4FM {

namespace {
constexpr int messageDelayMs = 250;
}

Label::Label(QWidget* parent, Qt::WindowFlags f) : QLabel(parent, f), lastWidth_(0) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    // set a minimum width so long texts don't force the main window to grow horizontally
    setMinimumWidth(fontMetrics().averageCharWidth() * 10);
}

// simplified QLabel::paintEvent without pixmap or shortcut support, but with middle eliding
void Label::paintEvent(QPaintEvent* /*event*/) {
    const QRect cr = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
    const QString txt = text();

    // recompute the elided text when the content or available width changes
    if (txt != lastText_ || cr.width() != lastWidth_) {
        lastText_ = txt;
        lastWidth_ = cr.width();
        elidedText_ = fontMetrics().elidedText(txt, Qt::ElideMiddle, cr.width());
    }

    if (!elidedText_.isEmpty()) {
        QPainter painter(this);
        QStyleOption opt;
        opt.initFrom(this);
        style()->drawItemText(&painter, cr, alignment(), opt.palette, isEnabled(), elidedText_, foregroundRole());
    }
}

StatusBar::StatusBar(QWidget* parent) : QStatusBar(parent), lastTimeOut_(0) {
    statusLabel_ = new Label(this);
    statusLabel_->setFrameShape(QFrame::NoFrame);
    // 4px space on both sides to visually separate this from permanent widgets
    statusLabel_->setContentsMargins(4, 0, 4, 0);
    addWidget(statusLabel_);

    messageTimer_ = new QTimer(this);
    messageTimer_->setSingleShot(true);
    messageTimer_->setInterval(messageDelayMs);
    connect(messageTimer_, &QTimer::timeout, this, &StatusBar::reallyShowMessage);
}

StatusBar::~StatusBar() = default;

void StatusBar::showMessage(const QString& message, int timeout) {
    // delay message display slightly to avoid flicker from very short lived messages
    lastMessage_ = message;
    lastTimeOut_ = timeout;

    if (messageTimer_ && !messageTimer_->isActive()) {
        messageTimer_->start();
    }
}

void StatusBar::reallyShowMessage() {
    if (lastTimeOut_ == 0) {
        // set the text directly on the label so it is not cleared by QAction hover messages
        // normalize newlines and tabs to spaces so eliding behaves predictably in a single line
        QString normalized = lastMessage_;
        normalized.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' '));
        statusLabel_->setText(normalized);
    }
    else {
        QStatusBar::showMessage(lastMessage_, lastTimeOut_);
    }
}

}  // namespace Oneg4FM
