/*
 * Hex editor view widget
 * src/ui/hexeditorview.h
 */

#ifndef PCMANFM_HEXEDITORVIEW_H
#define PCMANFM_HEXEDITORVIEW_H

#include <QAbstractScrollArea>
#include <QPointer>

#include <optional>
#include <utility>

#include "hexdocument.h"
#include "color_manager.h"

namespace Oneg4FM {

class HexEditorView : public QAbstractScrollArea {
    Q_OBJECT

   public:
    explicit HexEditorView(HexDocument* doc, QWidget* parent = nullptr);

    void setDocument(HexDocument* doc);
    void setColorManager(ColorManager* colors) {
        colors_ = colors;
        viewport()->update();
    }
    HexDocument* document() const { return doc_; }

    void setInsertMode(bool insert);
    bool insertMode() const { return insertMode_; }

    void setCursorOffset(std::uint64_t offset, bool keepAnchor = false);
    std::uint64_t cursorOffset() const { return cursorOffset_; }

    std::optional<std::pair<std::uint64_t, std::uint64_t>> selection() const;
    void setSelection(std::uint64_t start, std::uint64_t length);
    void clearSelection();

    void copySelectionToClipboard() const;
    void copySelectionAsHexToClipboard() const;
    bool pasteFromClipboard(QString& errorOut);
    bool deleteSelection(QString& errorOut);

   Q_SIGNALS:
    void cursorChanged(std::uint64_t offset, int byteValue, bool hasSelection);
    void modeChanged(bool insertMode);
    void statusMessage(const QString& message);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

   private:
    void ensureVisible();
    void updateAddressDigits();
    void updateScrollbars();
    bool selectionContains(std::uint64_t offset) const;
    int addressWidth(const QFontMetrics& fm) const;
    int hexColumnX(int byteIndex, const QFontMetrics& fm) const;
    int asciiColumnX(const QFontMetrics& fm) const;
    bool offsetFromPosition(const QPoint& pos, std::uint64_t& offsetOut, bool& inAscii, bool& highNibble) const;
    void moveCursorRelative(std::int64_t delta, bool keepAnchor);
    bool commitByteEdit(quint8 value, QString& errorOut);
    bool commitErase(std::uint64_t offset, std::uint64_t length, QString& errorOut);
    bool handleHexKey(int key, Qt::KeyboardModifiers mods, QString& errorOut);
    bool handleAsciiKey(const QString& text, QString& errorOut);
    void emitCursorInfo();

    QPointer<HexDocument> doc_;
    std::uint64_t cursorOffset_ = 0;
    bool cursorAscii_ = false;
    bool insertMode_ = false;
    bool highNibble_ = true;
    quint8 pendingNibble_ = 0;
    std::optional<std::uint64_t> anchor_;
    int bytesPerRow_ = 16;
    int addressDigits_ = 8;
    ColorManager* colors_ = nullptr;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_HEXEDITORVIEW_H
