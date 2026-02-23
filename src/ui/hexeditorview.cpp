/*
 * Hex editor view widget
 * src/ui/hexeditorview.cpp
 */

#include "hexeditorview.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QMessageBox>

#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

namespace Oneg4FM {

namespace {

bool isPrintable(char c) {
    return std::isprint(static_cast<unsigned char>(c)) != 0;
}

QByteArray hexStringToBytes(const QString& text, bool& okOut) {
    QByteArray out;
    QString cleaned = text;
    cleaned.remove(QLatin1Char(' '));
    cleaned.remove(QLatin1Char('\n'));
    cleaned.remove(QLatin1Char('\t'));
    if (cleaned.size() % 2 != 0) {
        okOut = false;
        return {};
    }
    out.resize(cleaned.size() / 2);
    bool ok = true;
    for (int i = 0; i < cleaned.size(); i += 2) {
        bool byteOk = false;
        const int value = cleaned.mid(i, 2).toInt(&byteOk, 16);
        if (!byteOk) {
            ok = false;
            break;
        }
        out[i / 2] = static_cast<char>(value);
    }
    okOut = ok;
    if (!ok) {
        out.clear();
    }
    return out;
}

QString bytesToHex(const QByteArray& data) {
    QString out;
    out.reserve(data.size() * 3);
    for (int i = 0; i < data.size(); ++i) {
        if (i != 0) {
            out.append(QLatin1Char(' '));
        }
        out.append(QStringLiteral("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QLatin1Char('0')).toUpper());
    }
    return out;
}

}  // namespace

HexEditorView::HexEditorView(HexDocument* doc, QWidget* parent) : QAbstractScrollArea(parent), doc_(doc) {
    setFocusPolicy(Qt::StrongFocus);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);

    if (doc_) {
        connect(doc_, &HexDocument::changed, this, [this] {
            const std::uint64_t sz = doc_ ? doc_->size() : 0;
            if (cursorOffset_ > sz) {
                cursorOffset_ = sz;
            }
            updateAddressDigits();
            updateScrollbars();
            viewport()->update();
            emitCursorInfo();
        });
    }

    updateAddressDigits();
    updateScrollbars();
}

void HexEditorView::setDocument(HexDocument* doc) {
    if (doc_ == doc) {
        return;
    }
    if (doc_) {
        disconnect(doc_, nullptr, this, nullptr);
    }
    doc_ = doc;
    if (doc_) {
        connect(doc_, &HexDocument::changed, this, [this] {
            const std::uint64_t sz = doc_ ? doc_->size() : 0;
            if (cursorOffset_ > sz) {
                cursorOffset_ = sz;
            }
            updateAddressDigits();
            updateScrollbars();
            viewport()->update();
            emitCursorInfo();
        });
    }
    updateAddressDigits();
    updateScrollbars();
    viewport()->update();
    emitCursorInfo();
}

void HexEditorView::setInsertMode(bool insert) {
    insertMode_ = insert;
    Q_EMIT modeChanged(insertMode_);
    emitCursorInfo();
}

void HexEditorView::setCursorOffset(std::uint64_t offset, bool keepAnchor) {
    if (!doc_) {
        return;
    }
    const std::uint64_t limit = doc_->size();
    cursorOffset_ = std::min<std::uint64_t>(offset, limit);
    if (!keepAnchor) {
        anchor_.reset();
    }
    highNibble_ = true;
    ensureVisible();
    viewport()->update();
    emitCursorInfo();
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> HexEditorView::selection() const {
    if (!anchor_) {
        return std::nullopt;
    }
    const std::uint64_t start = std::min<std::uint64_t>(*anchor_, cursorOffset_);
    std::uint64_t end = std::max<std::uint64_t>(*anchor_, cursorOffset_);
    if (!doc_) {
        return std::make_pair(start, end - start + 1);
    }
    const std::uint64_t size = doc_->size();
    if (start >= size) {
        return std::nullopt;
    }
    if (end >= size) {
        end = size ? size - 1 : 0;
    }
    const std::uint64_t len = end - start + 1;
    if (len == 0) {
        return std::nullopt;
    }
    return std::make_pair(start, len);
}

void HexEditorView::clearSelection() {
    anchor_.reset();
    viewport()->update();
    emitCursorInfo();
}

void HexEditorView::setSelection(std::uint64_t start, std::uint64_t length) {
    if (!doc_ || length == 0) {
        clearSelection();
        setCursorOffset(start);
        return;
    }
    const std::uint64_t limit = doc_->size();
    if (start >= limit) {
        clearSelection();
        setCursorOffset(limit);
        return;
    }
    const std::uint64_t end = std::min<std::uint64_t>(start + length - 1, limit ? limit - 1 : 0);
    anchor_ = start;
    cursorOffset_ = end;
    ensureVisible();
    viewport()->update();
    emitCursorInfo();
}
bool HexEditorView::selectionContains(std::uint64_t offset) const {
    const auto sel = selection();
    if (!sel) {
        return false;
    }
    return offset >= sel->first && offset < sel->first + sel->second;
}

void HexEditorView::copySelectionToClipboard() const {
    if (!doc_) {
        return;
    }
    const auto sel = selection();
    if (!sel) {
        return;
    }
    QByteArray data;
    QString error;
    if (!doc_->readBytes(sel->first, sel->second, data, error)) {
        return;
    }
    auto* mime = new QMimeData();
    mime->setData(QStringLiteral("application/octet-stream"), data);
    mime->setText(bytesToHex(data));
    QApplication::clipboard()->setMimeData(mime);
}

void HexEditorView::copySelectionAsHexToClipboard() const {
    if (!doc_) {
        return;
    }
    const auto sel = selection();
    if (!sel) {
        return;
    }
    QByteArray data;
    QString error;
    if (!doc_->readBytes(sel->first, sel->second, data, error)) {
        return;
    }
    QApplication::clipboard()->setText(bytesToHex(data));
}

bool HexEditorView::deleteSelection(QString& errorOut) {
    if (!doc_) {
        errorOut = tr("No document loaded.");
        return false;
    }
    const auto sel = selection();
    if (!sel) {
        return true;
    }
    if (!doc_->erase(sel->first, sel->second, errorOut)) {
        return false;
    }
    setCursorOffset(sel->first);
    anchor_.reset();
    viewport()->update();
    return true;
}

bool HexEditorView::pasteFromClipboard(QString& errorOut) {
    if (!doc_) {
        errorOut = tr("No document loaded.");
        return false;
    }
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    QByteArray data;
    if (mime->hasFormat(QStringLiteral("application/octet-stream"))) {
        data = mime->data(QStringLiteral("application/octet-stream"));
    }
    else if (mime->hasText()) {
        const QString text = mime->text();
        bool ok = false;
        data = hexStringToBytes(text.trimmed(), ok);
        if (!ok) {
            const QMessageBox::StandardButton choice = QMessageBox::question(
                window(), tr("Paste"), tr("Treat clipboard text as raw bytes? Choosing No will try to parse as hex."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (choice == QMessageBox::No) {
                bool hexOk = false;
                data = hexStringToBytes(text.trimmed(), hexOk);
                if (!hexOk) {
                    errorOut = tr("Clipboard text is not valid hex.");
                    return false;
                }
            }
            else {
                data = text.toUtf8();
            }
        }
    }
    if (data.isEmpty()) {
        return true;
    }

    auto sel = selection();
    if (sel) {
        if (!doc_->erase(sel->first, sel->second, errorOut)) {
            return false;
        }
        cursorOffset_ = sel->first;
        anchor_.reset();
    }

    const bool overwriteMode = !insertMode_ && cursorOffset_ < doc_->size();
    bool ok = false;
    if (overwriteMode) {
        ok = doc_->overwrite(cursorOffset_, data, errorOut);
    }
    else {
        ok = doc_->insert(cursorOffset_, data, errorOut);
    }
    if (!ok) {
        return false;
    }

    cursorOffset_ = cursorOffset_ + static_cast<std::uint64_t>(data.size());
    ensureVisible();
    viewport()->update();
    emitCursorInfo();
    return true;
}

int HexEditorView::addressWidth(const QFontMetrics& fm) const {
    return addressDigits_ * fm.horizontalAdvance(QLatin1Char('0')) + fm.horizontalAdvance(QLatin1Char(' '));
}

int HexEditorView::hexColumnX(int byteIndex, const QFontMetrics& fm) const {
    const int charWidth = fm.horizontalAdvance(QLatin1Char('0'));
    const int extraGap = (byteIndex >= 8) ? charWidth : 0;
    return addressWidth(fm) + byteIndex * charWidth * 3 + extraGap;
}

int HexEditorView::asciiColumnX(const QFontMetrics& fm) const {
    const int charWidth = fm.horizontalAdvance(QLatin1Char('0'));
    return addressWidth(fm) + (bytesPerRow_ * 3 + 1) * charWidth + charWidth;  // leave a spacer
}

bool HexEditorView::offsetFromPosition(const QPoint& pos,
                                       std::uint64_t& offsetOut,
                                       bool& inAscii,
                                       bool& highNibble) const {
    if (!doc_) {
        return false;
    }
    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
    const int row = pos.y() / lineHeight + verticalScrollBar()->value();
    if (row < 0) {
        return false;
    }
    const std::uint64_t baseOffset = static_cast<std::uint64_t>(row) * static_cast<std::uint64_t>(bytesPerRow_);
    if (baseOffset > doc_->size() && !insertMode_) {
        return false;
    }

    const int charWidth = fm.horizontalAdvance(QLatin1Char('0'));
    const int x = pos.x();
    const int asciiStart = asciiColumnX(fm);
    const int hexStart = addressWidth(fm);
    const int hexWidth = bytesPerRow_ * 3 * charWidth + charWidth;

    if (x >= asciiStart) {
        const int column = (x - asciiStart) / charWidth;
        if (column < 0 || column >= bytesPerRow_) {
            return false;
        }
        offsetOut = baseOffset + static_cast<std::uint64_t>(column);
        inAscii = true;
        highNibble = true;
        return true;
    }
    if (x >= hexStart && x < hexStart + hexWidth) {
        const int relative = x - hexStart;
        const int slot = relative / (3 * charWidth);
        if (slot < 0 || slot >= bytesPerRow_) {
            return false;
        }
        const int within = relative % (3 * charWidth);
        offsetOut = baseOffset + static_cast<std::uint64_t>(slot);
        inAscii = false;
        highNibble = within < (3 * charWidth) / 2;
        return true;
    }
    return false;
}

void HexEditorView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(viewport());
    const ColorScheme* scheme = colors_ ? &colors_->scheme() : nullptr;
    painter.fillRect(rect(), scheme ? scheme->background : palette().base());

    if (!doc_) {
        painter.drawText(rect(), Qt::AlignCenter, tr("No file loaded"));
        return;
    }

    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
    const int rowsVisible = viewport()->height() / lineHeight + 1;
    const int firstRow = verticalScrollBar()->value();
    const std::uint64_t startOffset = static_cast<std::uint64_t>(firstRow) * static_cast<std::uint64_t>(bytesPerRow_);

    const QColor highlightColor = palette().color(QPalette::Highlight);
    const QColor addrColor = scheme ? scheme->address.lighter(125) : palette().color(QPalette::Mid);
    const QColor byteColor = scheme ? scheme->bytes : palette().color(QPalette::Text);
    const QColor asciiColor = scheme ? scheme->bytes : palette().color(QPalette::Text);
    const QColor asciiNonPrintable = scheme ? scheme->bytes.darker(140) : palette().color(QPalette::Mid);
    const QColor patchedBg = scheme ? scheme->patchedBg : palette().color(QPalette::Link);

    for (int row = 0; row < rowsVisible; ++row) {
        const std::uint64_t rowOffset = startOffset + static_cast<std::uint64_t>(row) * bytesPerRow_;
        if (rowOffset > doc_->size()) {
            break;
        }

        QByteArray data;
        std::vector<bool> modified;
        QString error;
        if (!doc_->readBytesWithMarkers(rowOffset, bytesPerRow_, data, modified, error)) {
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(0, (row + 1) * lineHeight, error);
            break;
        }

        const int y = (row + 1) * lineHeight - fm.descent();

        // Address
        painter.setPen(addrColor);
        const QString addr = QStringLiteral("%1").arg(rowOffset, addressDigits_, 16, QLatin1Char('0')).toUpper();
        painter.drawText(0, y, addr);

        // Hex bytes
        for (int i = 0; i < bytesPerRow_; ++i) {
            const std::uint64_t byteOffset = rowOffset + static_cast<std::uint64_t>(i);
            const bool hasByte = i < data.size();
            const bool isCursor = byteOffset == cursorOffset_;
            const bool isSelected = selectionContains(byteOffset);

            QRect cellRect(hexColumnX(i, fm), y - lineHeight + fm.descent(),
                           fm.horizontalAdvance(QStringLiteral("FF ")), lineHeight);
            if (!isSelected && hasByte && modified.size() > static_cast<std::size_t>(i) &&
                modified[static_cast<std::size_t>(i)]) {
                painter.fillRect(cellRect.adjusted(0, 0, fm.horizontalAdvance(QLatin1Char('0')), 0), patchedBg);
            }
            if (isSelected) {
                painter.fillRect(cellRect.adjusted(0, 0, fm.horizontalAdvance(QLatin1Char('0')), 0), highlightColor);
            }

            if (hasByte) {
                const QString text =
                    QStringLiteral("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QLatin1Char('0')).toUpper();
                painter.setPen(byteColor);
                painter.drawText(cellRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            }
            else {
                painter.setPen(addrColor);
                painter.drawText(cellRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("  "));
            }

            if (isCursor && !cursorAscii_) {
                painter.setPen(palette().color(QPalette::Highlight));
                painter.drawRect(cellRect.adjusted(0, 1, 0, -1));
            }
        }

        // ASCII
        const int asciiX = asciiColumnX(fm);
        for (int i = 0; i < bytesPerRow_; ++i) {
            const std::uint64_t byteOffset = rowOffset + static_cast<std::uint64_t>(i);
            const bool hasByte = i < data.size();
            const bool isCursor = byteOffset == cursorOffset_;
            const bool isSelected = selectionContains(byteOffset);

            QRect cell(asciiX + i * fm.horizontalAdvance(QLatin1Char('0')), y - lineHeight + fm.descent(),
                       fm.horizontalAdvance(QLatin1Char('0')), lineHeight);
            if (isSelected) {
                painter.fillRect(cell, highlightColor);
            }
            if (hasByte) {
                const char c = data.at(i);
                const bool printable = isPrintable(c);
                painter.setPen(printable ? asciiColor : asciiNonPrintable);
                painter.drawText(cell, Qt::AlignLeft | Qt::AlignVCenter,
                                 printable ? QString(QChar::fromLatin1(c)) : QStringLiteral("."));
            }
            if (isCursor && cursorAscii_) {
                painter.setPen(palette().color(QPalette::Highlight));
                painter.drawRect(cell.adjusted(0, 1, 0, -1));
            }
        }
    }
}

void HexEditorView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollbars();
    viewport()->update();
}

void HexEditorView::wheelEvent(QWheelEvent* event) {
    QAbstractScrollArea::wheelEvent(event);
    viewport()->update();
}

void HexEditorView::updateAddressDigits() {
    if (!doc_) {
        addressDigits_ = 8;
        return;
    }
    std::uint64_t size = doc_->size();
    int digits = 8;
    while (size > 0) {
        size >>= 4;
        ++digits;
        if (digits >= 16) {
            break;
        }
    }
    addressDigits_ = std::min(16, std::max(8, digits));
}

void HexEditorView::updateScrollbars() {
    if (!doc_) {
        verticalScrollBar()->setRange(0, 0);
        return;
    }
    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
    const int rowsVisible = std::max(1, viewport()->height() / lineHeight);
    const std::uint64_t totalRows64 =
        (doc_->size() + static_cast<std::uint64_t>(bytesPerRow_) - 1) / static_cast<std::uint64_t>(bytesPerRow_);
    const int totalRows = totalRows64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                              ? std::numeric_limits<int>::max()
                              : static_cast<int>(totalRows64);
    verticalScrollBar()->setPageStep(rowsVisible);
    verticalScrollBar()->setRange(0, std::max(0, totalRows - rowsVisible));
}

void HexEditorView::moveCursorRelative(std::int64_t delta, bool keepAnchor) {
    if (!doc_) {
        return;
    }
    std::int64_t target = static_cast<std::int64_t>(cursorOffset_) + delta;
    if (target < 0) {
        target = 0;
    }
    const std::uint64_t maxPos = doc_->size();
    if (target > static_cast<std::int64_t>(maxPos)) {
        target = static_cast<std::int64_t>(maxPos);
    }
    setCursorOffset(static_cast<std::uint64_t>(target), keepAnchor);
}

void HexEditorView::ensureVisible() {
    const QFontMetrics fm(font());
    const int lineHeight = fm.height();
    const std::uint64_t row = cursorOffset_ / static_cast<std::uint64_t>(bytesPerRow_);
    const int topValue = verticalScrollBar()->value();
    const std::uint64_t topRow = static_cast<std::uint64_t>(std::max(0, topValue));
    const auto clampToInt = [](std::uint64_t value) {
        return value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                                   : static_cast<int>(value);
    };

    if (row < topRow) {
        verticalScrollBar()->setValue(clampToInt(row));
    }
    else {
        const int rowsVisible = std::max(1, viewport()->height() / lineHeight - 1);
        const std::uint64_t bottomRow = topRow + static_cast<std::uint64_t>(rowsVisible);
        if (row > bottomRow) {
            verticalScrollBar()->setValue(clampToInt(row - static_cast<std::uint64_t>(rowsVisible)));
        }
    }
}

bool HexEditorView::commitErase(std::uint64_t offset, std::uint64_t length, QString& errorOut) {
    if (!doc_) {
        errorOut = tr("No document loaded.");
        return false;
    }
    if (!doc_->erase(offset, length, errorOut)) {
        return false;
    }
    cursorOffset_ = offset;
    anchor_.reset();
    ensureVisible();
    viewport()->update();
    emitCursorInfo();
    return true;
}

bool HexEditorView::commitByteEdit(quint8 value, QString& errorOut) {
    if (!doc_) {
        errorOut = tr("No document loaded.");
        return false;
    }
    if (auto sel = selection()) {
        if (!doc_->erase(sel->first, sel->second, errorOut)) {
            return false;
        }
        cursorOffset_ = sel->first;
        anchor_.reset();
    }

    QByteArray data;
    data.append(static_cast<char>(value));
    bool ok = false;
    if (insertMode_ || cursorOffset_ >= doc_->size()) {
        ok = doc_->insert(cursorOffset_, data, errorOut);
    }
    else {
        ok = doc_->overwrite(cursorOffset_, data, errorOut);
    }
    if (!ok) {
        return false;
    }
    cursorOffset_ = std::min<std::uint64_t>(cursorOffset_ + 1, doc_->size());
    highNibble_ = true;
    ensureVisible();
    viewport()->update();
    emitCursorInfo();
    return true;
}

bool HexEditorView::handleHexKey(int key, Qt::KeyboardModifiers mods, QString& errorOut) {
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    int value = -1;
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        value = key - Qt::Key_0;
    }
    else if (key >= Qt::Key_A && key <= Qt::Key_F) {
        value = 10 + (key - Qt::Key_A);
    }
    if (value < 0) {
        return false;
    }

    if (highNibble_) {
        pendingNibble_ = static_cast<quint8>(value << 4);
        highNibble_ = false;
        return true;
    }
    const quint8 byteValue = static_cast<quint8>(pendingNibble_ | static_cast<quint8>(value & 0x0F));
    highNibble_ = true;
    return commitByteEdit(byteValue, errorOut);
}

bool HexEditorView::handleAsciiKey(const QString& text, QString& errorOut) {
    if (text.isEmpty()) {
        return false;
    }
    const char c = text.at(0).toLatin1();
    if (!isPrintable(c)) {
        return false;
    }
    highNibble_ = true;
    return commitByteEdit(static_cast<quint8>(c), errorOut);
}

void HexEditorView::emitCursorInfo() {
    int value = -1;
    if (doc_ && doc_->size() > 0 && cursorOffset_ < doc_->size()) {
        QByteArray data;
        QString error;
        if (doc_->readBytes(cursorOffset_, 1, data, error) && !data.isEmpty()) {
            value = static_cast<unsigned char>(data.at(0));
        }
    }
    Q_EMIT cursorChanged(cursorOffset_, value, selection().has_value());
}

void HexEditorView::keyPressEvent(QKeyEvent* event) {
    if (!doc_) {
        event->ignore();
        return;
    }

    QString error;
    const bool keepAnchor = event->modifiers() & Qt::ShiftModifier;
    switch (event->key()) {
        case Qt::Key_Left:
            moveCursorRelative(-1, keepAnchor);
            event->accept();
            return;
        case Qt::Key_Right:
            moveCursorRelative(1, keepAnchor);
            event->accept();
            return;
        case Qt::Key_Up:
            moveCursorRelative(-bytesPerRow_, keepAnchor);
            event->accept();
            return;
        case Qt::Key_Down:
            moveCursorRelative(bytesPerRow_, keepAnchor);
            event->accept();
            return;
        case Qt::Key_PageUp: {
            const int page = std::max(1, viewport()->height() / fontMetrics().height());
            moveCursorRelative(-static_cast<std::int64_t>(page) * bytesPerRow_, keepAnchor);
            event->accept();
            return;
        }
        case Qt::Key_PageDown: {
            const int page = std::max(1, viewport()->height() / fontMetrics().height());
            moveCursorRelative(static_cast<std::int64_t>(page) * bytesPerRow_, keepAnchor);
            event->accept();
            return;
        }
        case Qt::Key_Home:
            setCursorOffset(
                event->modifiers() & Qt::ControlModifier ? 0 : (cursorOffset_ / bytesPerRow_) * bytesPerRow_,
                keepAnchor);
            event->accept();
            return;
        case Qt::Key_End: {
            const std::uint64_t target = (event->modifiers() & Qt::ControlModifier)
                                             ? (doc_->size())
                                             : ((cursorOffset_ / bytesPerRow_) * bytesPerRow_ + bytesPerRow_ - 1);
            setCursorOffset(std::min<std::uint64_t>(target, doc_->size()), keepAnchor);
            event->accept();
            return;
        }
        case Qt::Key_Insert:
            setInsertMode(!insertMode_);
            event->accept();
            return;
        case Qt::Key_Backspace:
            if (selection()) {
                deleteSelection(error);
            }
            else if (cursorOffset_ > 0) {
                commitErase(cursorOffset_ - 1, 1, error);
            }
            event->accept();
            return;
        case Qt::Key_Delete:
            if (selection()) {
                deleteSelection(error);
            }
            else {
                commitErase(cursorOffset_, 1, error);
            }
            event->accept();
            return;
        case Qt::Key_Tab:
            cursorAscii_ = !cursorAscii_;
            viewport()->update();
            event->accept();
            return;
        default:
            break;
    }

    if (event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteFromClipboard(error);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        copySelectionToClipboard();
        deleteSelection(error);
        event->accept();
        return;
    }

    if (!cursorAscii_) {
        if (handleHexKey(event->key(), event->modifiers(), error)) {
            event->accept();
            if (!error.isEmpty()) {
                Q_EMIT statusMessage(error);
            }
            return;
        }
    }
    if (handleAsciiKey(event->text(), error)) {
        event->accept();
        if (!error.isEmpty()) {
            Q_EMIT statusMessage(error);
        }
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void HexEditorView::mousePressEvent(QMouseEvent* event) {
    if (!doc_) {
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    std::uint64_t offset = 0;
    bool inAscii = false;
    bool highNibble = true;
    if (!offsetFromPosition(event->pos(), offset, inAscii, highNibble)) {
        return;
    }
    cursorAscii_ = inAscii;
    highNibble_ = highNibble;
    anchor_ = offset;
    setCursorOffset(offset, true);
    viewport()->update();
}

void HexEditorView::mouseMoveEvent(QMouseEvent* event) {
    if (!doc_) {
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    std::uint64_t offset = 0;
    bool inAscii = false;
    bool highNibble = false;
    if (!offsetFromPosition(event->pos(), offset, inAscii, highNibble)) {
        return;
    }
    setCursorOffset(offset, true);
    viewport()->update();
}

}  // namespace Oneg4FM
