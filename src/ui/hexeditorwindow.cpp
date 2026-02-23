/*
 * Hex editor window
 * src/ui/hexeditorwindow.cpp
 */

#include "hexeditorwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStringList>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QLabel>
#include <QDockWidget>
#include <QFormLayout>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "color_manager.h"

#include <algorithm>
#include <cctype>
#include <vector>
#include <cstring>
#include <cmath>

namespace PCManFM {

namespace {

QByteArray parseHexString(const QString& text, bool& okOut) {
    QByteArray out;
    QString cleaned = text;
    cleaned.remove(QLatin1Char(' '));
    cleaned.remove(QLatin1Char('\t'));
    cleaned.remove(QLatin1Char('\n'));
    if (cleaned.isEmpty() || (cleaned.size() % 2) != 0) {
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

}  // namespace

HexEditorWindow::HexEditorWindow(QWidget* parent) : QMainWindow(parent), doc_(std::make_unique<HexDocument>()) {
    setupUi();
    updateWindowTitle();
}

HexEditorWindow::~HexEditorWindow() = default;

void HexEditorWindow::setupUi() {
    colors_ = std::make_unique<ColorManager>(this);
    view_ = new HexEditorView(doc_.get(), this);
    view_->setColorManager(colors_.get());
    setCentralWidget(view_);
    statusBar()->setSizeGripEnabled(false);

    auto* toolbar = addToolBar(tr("Hex Editor"));
    toolbar->setMovable(false);

    saveAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-save")), tr("Save"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { doSave(false); });

    saveAsAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-save-as")), tr("Save As…"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { doSave(true); });

    toolbar->addSeparator();

    undoAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), tr("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!doc_->undo(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Undo failed"), err);
        }
    });

    redoAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-redo")), tr("Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!doc_->redo(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Redo failed"), err);
        }
    });

    toolbar->addSeparator();

    copyAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy"));
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, view_, &HexEditorView::copySelectionToClipboard);

    copyHexAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("code-context")), tr("Copy as Hex"));
    connect(copyHexAction_, &QAction::triggered, view_, &HexEditorView::copySelectionAsHexToClipboard);

    pasteAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), tr("Paste"));
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!view_->pasteFromClipboard(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Paste failed"), err);
        }
    });

    deleteAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!view_->deleteSelection(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Delete failed"), err);
        }
    });

    toolbar->addSeparator();

    findAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), tr("Find…"));
    findAction_->setShortcut(QKeySequence::Find);
    connect(findAction_, &QAction::triggered, this, [this] { performFind(true); });

    findNextAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-down")), tr("Find Next"));
    findNextAction_->setShortcut(QKeySequence::FindNext);
    connect(findNextAction_, &QAction::triggered, this, [this] { performFind(true); });

    findPrevAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-up")), tr("Find Previous"));
    findPrevAction_->setShortcut(QKeySequence::FindPrevious);
    connect(findPrevAction_, &QAction::triggered, this, [this] { performFind(false); });

    gotoAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-jump")), tr("Go to Offset…"));
    gotoAction_->setShortcut(QKeySequence(tr("Ctrl+G")));
    connect(gotoAction_, &QAction::triggered, this, &HexEditorWindow::goToOffset);

    toolbar->addSeparator();

    replaceAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find-replace")), tr("Replace…"));
    replaceAction_->setShortcut(QKeySequence::Replace);
    connect(replaceAction_, &QAction::triggered, this, [this] { performReplace(false); });

    replaceAllAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find-replace")), tr("Replace All"));
    connect(replaceAllAction_, &QAction::triggered, this, [this] { performReplace(true); });

    findAllAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), tr("Find All"));
    connect(findAllAction_, &QAction::triggered, this, [this] {
        performFind(true);
        std::vector<std::uint64_t> offsets;
        QString err;
        if (!doc_->findAll(lastSearch_, offsets, err)) {
            if (!err.isEmpty()) {
                QMessageBox::warning(this, tr("Find All"), err);
            }
            return;
        }
        QMessageBox::information(this, tr("Find All"),
                                 tr("Found %1 occurrence(s).").arg(static_cast<int>(offsets.size())));
    });

    toolbar->addSeparator();

    nextModifiedAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-next")), tr("Next Modified"));
    connect(nextModifiedAction_, &QAction::triggered, this, [this] { jumpToModified(true); });
    prevModifiedAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-previous")), tr("Previous Modified"));
    connect(prevModifiedAction_, &QAction::triggered, this, [this] { jumpToModified(false); });

    checksumAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-open-recent")), tr("Checksum…"));
    connect(checksumAction_, &QAction::triggered, this, [this] { computeChecksums(true); });

    statsAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-statistics")), tr("Byte Stats…"));
    connect(statsAction_, &QAction::triggered, this, [this] { computeByteStats(true); });

    bookmarkAddAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), tr("Add Bookmark"));
    connect(bookmarkAddAction_, &QAction::triggered, this, &HexEditorWindow::addBookmark);
    bookmarkPrevAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-previous")), tr("Prev Bookmark"));
    connect(bookmarkPrevAction_, &QAction::triggered, this, [this] { jumpBookmark(false); });
    bookmarkNextAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-next")), tr("Next Bookmark"));
    connect(bookmarkNextAction_, &QAction::triggered, this, [this] { jumpBookmark(true); });
    bookmarkListAction_ =
        toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-list-bookmarks")), tr("List Bookmarks"));
    connect(bookmarkListAction_, &QAction::triggered, this, &HexEditorWindow::listBookmarks);

    diffAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("diff")), tr("Diff With File…"));
    connect(diffAction_, &QAction::triggered, this, &HexEditorWindow::diffWithFile);
    nextDiffAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-down")), tr("Next Diff"));
    connect(nextDiffAction_, &QAction::triggered, this, [this] { nextDiff(true); });
    prevDiffAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-up")), tr("Prev Diff"));
    connect(prevDiffAction_, &QAction::triggered, this, [this] { nextDiff(false); });
    diffSideBySideAction_ =
        toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-split-left-right")), tr("Side-by-Side Diff…"));
    connect(diffSideBySideAction_, &QAction::triggered, this, &HexEditorWindow::sideBySideDiff);

    insertToggleAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("insert-text")), tr("Insert mode"));
    insertToggleAction_->setCheckable(true);
    insertToggleAction_->setChecked(false);
    connect(insertToggleAction_, &QAction::toggled, this, [this](bool checked) { view_->setInsertMode(checked); });

    connect(view_, &HexEditorView::cursorChanged, this, &HexEditorWindow::updateStatus);
    connect(view_, &HexEditorView::modeChanged, this, [this](bool checked) {
        insertToggleAction_->setChecked(checked);
        updateWindowTitle();
    });
    connect(view_, &HexEditorView::statusMessage, this, [this](const QString& msg) { statusBar()->showMessage(msg); });

    connect(doc_.get(), &HexDocument::changed, this, [this]() {
        updateWindowTitle();
        updateActionStates(view_ && view_->selection().has_value());
    });
    connect(doc_.get(), &HexDocument::saved, this, &HexEditorWindow::updateWindowTitle);

    changeTimer_ = new QTimer(this);
    changeTimer_->setInterval(2000);
    connect(changeTimer_, &QTimer::timeout, this, &HexEditorWindow::checkExternalChanges);
    changeTimer_->start();

    modeLabel_ = new QLabel(this);
    modifiedLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(modeLabel_);
    statusBar()->addPermanentWidget(modifiedLabel_);

    inspectorDock_ = new QDockWidget(tr("Inspect"), this);
    inspectorDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* inspector = new QWidget(inspectorDock_);
    auto* form = new QFormLayout(inspector);
    inspectorOffset_ = new QLabel(tr("0x0 / 0"), inspector);
    inspectorU8_ = new QLabel(QString(), inspector);
    inspectorI8_ = new QLabel(QString(), inspector);
    inspectorU16LE_ = new QLabel(QString(), inspector);
    inspectorU16BE_ = new QLabel(QString(), inspector);
    inspectorU32LE_ = new QLabel(QString(), inspector);
    inspectorU32BE_ = new QLabel(QString(), inspector);
    inspectorU64LE_ = new QLabel(QString(), inspector);
    inspectorU64BE_ = new QLabel(QString(), inspector);
    inspectorFloat_ = new QLabel(QString(), inspector);
    inspectorDouble_ = new QLabel(QString(), inspector);
    inspectorUtf8_ = new QLabel(QString(), inspector);

    form->addRow(tr("Offset"), inspectorOffset_);
    form->addRow(tr("U8"), inspectorU8_);
    form->addRow(tr("I8"), inspectorI8_);
    form->addRow(tr("U16 LE"), inspectorU16LE_);
    form->addRow(tr("U16 BE"), inspectorU16BE_);
    form->addRow(tr("U32 LE"), inspectorU32LE_);
    form->addRow(tr("U32 BE"), inspectorU32BE_);
    form->addRow(tr("U64 LE"), inspectorU64LE_);
    form->addRow(tr("U64 BE"), inspectorU64BE_);
    form->addRow(tr("Float"), inspectorFloat_);
    form->addRow(tr("Double"), inspectorDouble_);
    form->addRow(tr("UTF-8"), inspectorUtf8_);

    inspector->setLayout(form);
    inspectorDock_->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);

    updateActionStates(false);
    statusBar()->showMessage(tr("Ready"));
}

bool HexEditorWindow::openFile(const QString& path, QString& errorOut) {
    lastSearch_.clear();
    const bool ok = doc_->openFile(path, errorOut);
    if (ok) {
        view_->setCursorOffset(0);
        view_->clearSelection();
        updateWindowTitle();
        updateActionStates(false);
        if (!doc_->isRegularFile()) {
            QMessageBox::warning(this, tr("Hex editor"),
                                 tr("The selected path is not a regular file; editing may not be safe."));
        }
        updateInspector(0);
    }
    return ok;
}

void HexEditorWindow::updateWindowTitle() {
    QString title = tr("Hex Editor");
    if (doc_ && !doc_->path().isEmpty()) {
        QFileInfo info(doc_->path());
        title = info.fileName().isEmpty() ? doc_->path() : info.fileName();
    }
    if (doc_ && doc_->modified()) {
        title = QStringLiteral("* %1").arg(title);
    }
    setWindowTitle(title);
}

void HexEditorWindow::updateStatus(std::uint64_t offset, int byteValue, bool hasSelection) {
    QStringList parts;
    parts << tr("Offset 0x%1 (%2)").arg(offset, 0, 16).arg(offset);
    if (byteValue >= 0) {
        parts << tr("Byte 0x%1 (%2)").arg(byteValue, 2, 16, QLatin1Char('0')).arg(byteValue);
    }
    if (hasSelection && view_) {
        const auto sel = view_->selection();
        if (sel) {
            parts << tr("%1 bytes selected").arg(sel->second);
        }
    }
    const bool insert = view_ && view_->insertMode();
    parts << (insert ? tr("INS") : tr("OVR"));
    statusBar()->showMessage(parts.join(QStringLiteral(" | ")));
    if (modeLabel_) {
        modeLabel_->setText(insert ? tr("Insert") : tr("Overwrite"));
    }
    const bool modified = doc_ && doc_->isModified(offset);
    if (modifiedLabel_) {
        modifiedLabel_->setText(modified ? tr("Modified") : QString());
    }
    updateInspector(offset);
    updateActionStates(hasSelection);
}

void HexEditorWindow::updateActionStates(bool hasSelection) {
    const bool hasDoc = doc_ != nullptr;
    const bool modified = hasDoc && doc_->modified();
    if (saveAction_) {
        saveAction_->setEnabled(modified);
    }
    if (undoAction_) {
        undoAction_->setEnabled(hasDoc);
    }
    if (redoAction_) {
        redoAction_->setEnabled(hasDoc);
    }
    if (copyAction_) {
        copyAction_->setEnabled(hasSelection);
    }
    if (copyHexAction_) {
        copyHexAction_->setEnabled(hasSelection);
    }
    if (deleteAction_) {
        deleteAction_->setEnabled(hasSelection);
    }
    if (findAction_) {
        findAction_->setEnabled(hasDoc);
    }
    if (findAllAction_) {
        findAllAction_->setEnabled(hasDoc);
    }
    if (replaceAction_) {
        replaceAction_->setEnabled(hasDoc);
    }
    if (replaceAllAction_) {
        replaceAllAction_->setEnabled(hasDoc);
    }
    if (nextModifiedAction_) {
        nextModifiedAction_->setEnabled(hasDoc);
    }
    if (prevModifiedAction_) {
        prevModifiedAction_->setEnabled(hasDoc);
    }
    if (checksumAction_) {
        checksumAction_->setEnabled(hasDoc);
    }
    if (statsAction_) {
        statsAction_->setEnabled(hasDoc);
    }
    if (bookmarkAddAction_) {
        bookmarkAddAction_->setEnabled(hasDoc);
    }
    if (bookmarkPrevAction_) {
        bookmarkPrevAction_->setEnabled(hasDoc && !bookmarks_.empty());
    }
    if (bookmarkNextAction_) {
        bookmarkNextAction_->setEnabled(hasDoc && !bookmarks_.empty());
    }
    if (bookmarkListAction_) {
        bookmarkListAction_->setEnabled(hasDoc && !bookmarks_.empty());
    }
    if (diffAction_) {
        diffAction_->setEnabled(hasDoc);
    }
    const bool hasDiffs = hasDoc && !diffOffsets_.empty();
    if (nextDiffAction_) {
        nextDiffAction_->setEnabled(hasDiffs);
    }
    if (prevDiffAction_) {
        prevDiffAction_->setEnabled(hasDiffs);
    }
    if (diffSideBySideAction_) {
        diffSideBySideAction_->setEnabled(hasDoc);
    }
}

bool HexEditorWindow::promptToSave() {
    if (!doc_ || !doc_->modified()) {
        return true;
    }
    const auto res = QMessageBox::question(
        this, tr("Unsaved changes"), tr("The file has unsaved changes. Do you want to save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (res == QMessageBox::Cancel) {
        return false;
    }
    if (res == QMessageBox::Discard) {
        return true;
    }

    doSave(false);
    if (doc_->modified()) {
        return false;
    }
    return true;
}

void HexEditorWindow::doSave(bool saveAs) {
    if (!doc_) {
        return;
    }
    QString err;
    QString targetPath = doc_->path();
    if (saveAs) {
        const QString chosen = QFileDialog::getSaveFileName(this, tr("Save File"), targetPath);
        if (chosen.isEmpty()) {
            return;
        }
        targetPath = chosen;
        QFileInfo info(targetPath);
        if (!info.isFile() && info.exists()) {
            QMessageBox::warning(this, tr("Save failed"), tr("Target is not a regular file."));
            return;
        }
        if (!doc_->saveAs(targetPath, err)) {
            QMessageBox::warning(this, tr("Save failed"), err);
            return;
        }
    }
    else {
        if (!doc_->save(err)) {
            QMessageBox::warning(this, tr("Save failed"), err);
            return;
        }
    }
    updateWindowTitle();
    updateActionStates(view_ && view_->selection().has_value());
}

void HexEditorWindow::checkExternalChanges() {
    if (!doc_ || doc_->path().isEmpty()) {
        return;
    }

    bool changed = false;
    QString err;
    if (!doc_->hasExternalChange(changed, err)) {
        if (!err.isEmpty()) {
            statusBar()->showMessage(err);
        }
        return;
    }
    quint64 fingerprint = 0;
    if (!doc_->currentFingerprint(fingerprint, err)) {
        if (!err.isEmpty()) {
            statusBar()->showMessage(err);
        }
        return;
    }

    if (!changed) {
        suppressExternalPrompt_ = false;
        lastExternalFingerprint_ = fingerprint;
        return;
    }
    if (suppressExternalPrompt_ && fingerprint == lastExternalFingerprint_) {
        return;
    }
    lastExternalFingerprint_ = fingerprint;

    const QMessageBox::StandardButton res = QMessageBox::warning(
        this, tr("File changed on disk"),
        tr("The file has changed on disk. Reloading will discard your unsaved edits. What do you want to do?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Save, QMessageBox::Yes);

    if (res == QMessageBox::No) {
        suppressExternalPrompt_ = true;
        return;
    }
    if (res == QMessageBox::Save) {
        doSave(false);
        suppressExternalPrompt_ = false;
        return;
    }

    if (res == QMessageBox::Yes) {
        if (!doc_->modified()) {
            if (!doc_->reload(err)) {
                QMessageBox::warning(this, tr("Reload failed"), err);
                return;
            }
            view_->setCursorOffset(0);
            view_->clearSelection();
            updateWindowTitle();
            updateInspector(0);
            return;
        }

        const auto confirm = QMessageBox::question(this, tr("Reload file"),
                                                   tr("Discard your unsaved changes and reload the file from disk?"),
                                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm == QMessageBox::Yes) {
            if (!doc_->reload(err)) {
                QMessageBox::warning(this, tr("Reload failed"), err);
                return;
            }
            view_->setCursorOffset(0);
            view_->clearSelection();
            updateWindowTitle();
            updateInspector(0);
        }
    }
}

QByteArray HexEditorWindow::parseSearchInput(const QString& input, bool& okOut) const {
    okOut = false;
    if (input.trimmed().isEmpty()) {
        return {};
    }
    bool okHex = false;
    const QByteArray hex = parseHexString(input.trimmed(), okHex);
    if (okHex) {
        okOut = true;
        return hex;
    }
    okOut = true;
    return input.toUtf8();
}

QByteArray HexEditorWindow::promptForPattern(const QString& title, bool& okOut) {
    okOut = false;
    QStringList history = statusBar()->property("hexSearchHistory").toStringList();
    const QString initial = history.isEmpty() ? QString() : history.back();
    const QString input = QInputDialog::getText(this, title, tr("Enter hex (e.g. 0A FF) or text to search for:"),
                                                QLineEdit::Normal, initial, &okOut);
    if (!okOut || input.isEmpty()) {
        return {};
    }
    bool parseOk = false;
    QByteArray pattern = parseSearchInput(input, parseOk);
    if (parseOk && !pattern.isEmpty()) {
        if (!history.contains(input)) {
            history.append(input);
            if (history.size() > 10) {
                history.pop_front();
            }
            statusBar()->setProperty("hexSearchHistory", history);
        }
    }
    okOut = parseOk;
    return pattern;
}

QByteArray HexEditorWindow::promptForReplace(bool& okOut) {
    QStringList history = statusBar()->property("hexReplaceHistory").toStringList();
    const QString initial = history.isEmpty() ? QString() : history.back();
    const QString input = QInputDialog::getText(this, tr("Replace"), tr("Replace with (hex or text):"),
                                                QLineEdit::Normal, initial, &okOut);
    if (!okOut) {
        return {};
    }
    bool parseOk = false;
    QByteArray pattern = parseSearchInput(input, parseOk);
    if (parseOk) {
        if (!history.contains(input)) {
            history.append(input);
            if (history.size() > 10) {
                history.pop_front();
            }
            statusBar()->setProperty("hexReplaceHistory", history);
        }
    }
    okOut = parseOk;
    return pattern;
}

void HexEditorWindow::performFind(bool forward) {
    if (!doc_ || !view_) {
        return;
    }
    if (sender() == findAction_) {
        lastSearch_.clear();
    }
    if (lastSearch_.isEmpty()) {
        bool ok = false;
        lastSearch_ = promptForPattern(tr("Find"), ok);
        if (!ok || lastSearch_.isEmpty()) {
            return;
        }
    }

    const auto sel = view_->selection();
    std::uint64_t start = view_->cursorOffset();
    if (sel) {
        start = forward ? sel->first + sel->second : (sel->first == 0 ? 0 : sel->first - 1);
    }

    std::uint64_t found = 0;
    QString error;
    const bool ok = forward ? doc_->findForward(lastSearch_, start, found, error)
                            : doc_->findBackward(lastSearch_, start, found, error);
    if (ok) {
        view_->setSelection(found, static_cast<std::uint64_t>(lastSearch_.size()));
        statusBar()->showMessage(tr("Found at offset 0x%1").arg(found, 0, 16));
        return;
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Find"), error);
    }
    else {
        QMessageBox::information(this, tr("Find"), tr("Pattern not found."));
    }
}

void HexEditorWindow::performReplace(bool replaceAll) {
    if (!doc_ || !view_) {
        return;
    }
    const bool freshInput = lastSearch_.isEmpty() || sender() == replaceAction_;
    if (freshInput) {
        bool ok = false;
        lastSearch_ = promptForPattern(tr("Replace"), ok);
        if (!ok || lastSearch_.isEmpty()) {
            return;
        }
        lastReplace_ = promptForReplace(ok);
        if (!ok) {
            lastSearch_.clear();
            return;
        }
    }

    if (lastSearch_.isEmpty()) {
        return;
    }

    QString error;
    if (replaceAll) {
        std::vector<std::uint64_t> offsets;
        if (!doc_->findAll(lastSearch_, offsets, error)) {
            if (!error.isEmpty()) {
                QMessageBox::warning(this, tr("Replace All"), error);
            }
            return;
        }
        std::size_t count = 0;
        for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
            if (!doc_->erase(*it, static_cast<std::uint64_t>(lastSearch_.size()), error)) {
                break;
            }
            if (!lastReplace_.isEmpty()) {
                if (!doc_->insert(*it, lastReplace_, error)) {
                    break;
                }
            }
            count++;
        }
        statusBar()->showMessage(tr("Replaced %1 occurrence(s).").arg(static_cast<int>(count)));
        return;
    }

    auto sel = view_->selection();
    std::uint64_t start = view_->cursorOffset();
    if (sel) {
        start = sel->first + sel->second;
    }
    std::uint64_t found = 0;
    if (!doc_->findForward(lastSearch_, start, found, error)) {
        if (!error.isEmpty()) {
            QMessageBox::warning(this, tr("Replace"), error);
        }
        else {
            QMessageBox::information(this, tr("Replace"), tr("Pattern not found."));
        }
        return;
    }
    if (!doc_->erase(found, static_cast<std::uint64_t>(lastSearch_.size()), error)) {
        QMessageBox::warning(this, tr("Replace"), error);
        return;
    }
    if (!lastReplace_.isEmpty()) {
        if (!doc_->insert(found, lastReplace_, error)) {
            QMessageBox::warning(this, tr("Replace"), error);
            return;
        }
    }
    view_->setSelection(found, static_cast<std::uint64_t>(lastReplace_.size()));
    view_->setCursorOffset(found + static_cast<std::uint64_t>(lastReplace_.size()));
    statusBar()->showMessage(tr("Replaced at offset 0x%1").arg(found, 0, 16));
}

void HexEditorWindow::jumpToModified(bool forward) {
    if (!doc_ || !view_) {
        return;
    }
    std::uint64_t start = view_->cursorOffset();
    std::uint64_t found = 0;
    if (!doc_->nextModifiedOffset(start, forward, found)) {
        statusBar()->showMessage(tr("No further modified bytes."));
        return;
    }
    view_->setSelection(found, 1);
    view_->setCursorOffset(found, true);
    statusBar()->showMessage(tr("Jumped to modified byte at 0x%1").arg(found, 0, 16));
}

bool HexEditorWindow::iterateRange(std::uint64_t start,
                                   std::uint64_t length,
                                   const std::function<bool(const QByteArray&)>& consumer,
                                   QString& errorOut) {
    if (!doc_) {
        errorOut = tr("No document loaded.");
        return false;
    }
    constexpr std::uint64_t kChunk = 1 * 1024 * 1024;
    std::uint64_t remaining = length;
    std::uint64_t pos = start;
    while (remaining > 0) {
        const std::uint64_t chunk = std::min<std::uint64_t>(remaining, kChunk);
        QByteArray data;
        if (!doc_->readBytes(pos, chunk, data, errorOut)) {
            return false;
        }
        if (!consumer(data)) {
            return false;
        }
        remaining -= chunk;
        pos += chunk;
    }
    return true;
}

void HexEditorWindow::computeChecksums(bool selectionOnly) {
    if (!doc_) {
        return;
    }
    auto sel = view_ ? view_->selection() : std::nullopt;
    std::uint64_t start = 0;
    std::uint64_t length = doc_->size();
    if (selectionOnly && sel) {
        start = sel->first;
        length = sel->second;
    }
    if (length == 0) {
        QMessageBox::information(this, tr("Checksum"), tr("Nothing to hash."));
        return;
    }

    struct ChecksumResult {
        bool ok = false;
        QString error;
        quint32 crc = 0;
        QByteArray sha;
    };

    auto* watcher = new QFutureWatcher<ChecksumResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, start, length]() {
        const auto result = watcher->future().result();
        watcher->deleteLater();
        if (!result.ok) {
            if (!result.error.isEmpty()) {
                QMessageBox::warning(this, tr("Checksum"), result.error);
            }
            return;
        }
        const QString message = tr("Range: 0x%1–0x%2 (%3 bytes)\nCRC32: %4\nSHA-256: %5")
                                    .arg(start, 0, 16)
                                    .arg(start + length, 0, 16)
                                    .arg(length)
                                    .arg(QStringLiteral("%1").arg(result.crc, 8, 16, QLatin1Char('0')).toUpper())
                                    .arg(QString::fromLatin1(result.sha.toHex()));
        QMessageBox::information(this, tr("Checksum"), message);
    });

    HexDocument* doc = doc_.get();
    watcher->setFuture(QtConcurrent::run([doc, start, length]() -> ChecksumResult {
        ChecksumResult res;
        QCryptographicHash sha256(QCryptographicHash::Sha256);
        quint32 crc = 0xFFFFFFFFu;
        QString error;
        QByteArray data;
        const std::uint64_t kChunk = 2 * 1024 * 1024;
        std::uint64_t remaining = length;
        std::uint64_t pos = start;

        while (remaining > 0) {
            const std::uint64_t chunk = std::min<std::uint64_t>(remaining, kChunk);
            if (!doc->readBytes(pos, chunk, data, error)) {
                res.ok = false;
                res.error = error;
                return res;
            }
            sha256.addData(data);
            for (unsigned char b : data) {
                crc ^= b;
                for (int i = 0; i < 8; ++i) {
                    const bool lsb = crc & 1u;
                    crc >>= 1;
                    if (lsb) {
                        crc ^= 0xEDB88320u;
                    }
                }
            }
            remaining -= chunk;
            pos += chunk;
        }

        res.ok = true;
        res.crc = crc ^ 0xFFFFFFFFu;
        res.sha = sha256.result();
        return res;
    }));
    statusBar()->showMessage(tr("Computing checksums…"), 2000);
}

void HexEditorWindow::computeByteStats(bool selectionOnly) {
    if (!doc_) {
        return;
    }
    auto sel = view_ ? view_->selection() : std::nullopt;
    std::uint64_t start = 0;
    std::uint64_t length = doc_->size();
    if (selectionOnly && sel) {
        start = sel->first;
        length = sel->second;
    }
    if (length == 0) {
        QMessageBox::information(this, tr("Byte stats"), tr("Nothing to analyze."));
        return;
    }

    struct StatsPartial {
        std::array<std::uint64_t, 256> counts{};
        QString error;
        bool ok = true;
    };

    struct StatsResult {
        bool ok = true;
        QString error;
        std::array<std::uint64_t, 256> counts{};
    };

    std::vector<std::pair<std::uint64_t, std::uint64_t>> chunks;
    const std::uint64_t kChunk = 2 * 1024 * 1024;
    std::uint64_t pos = start;
    std::uint64_t remaining = length;
    while (remaining > 0) {
        const std::uint64_t chunk = std::min<std::uint64_t>(remaining, kChunk);
        chunks.emplace_back(pos, chunk);
        pos += chunk;
        remaining -= chunk;
    }

    auto* watcher = new QFutureWatcher<StatsResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, start, length]() {
        const auto result = watcher->future().result();
        watcher->deleteLater();
        if (!result.ok) {
            QMessageBox::warning(this, tr("Byte stats"), result.error);
            return;
        }

        double entropy = 0.0;
        for (std::uint64_t c : result.counts) {
            if (c == 0) {
                continue;
            }
            const double p = static_cast<double>(c) / static_cast<double>(length);
            entropy -= p * std::log2(p);
        }

        QStringList lines;
        lines << tr("Range: 0x%1–0x%2 (%3 bytes)").arg(start, 0, 16).arg(start + length, 0, 16).arg(length);
        lines << tr("Entropy: %1 bits/byte").arg(entropy, 0, 'f', 4);
        lines << QString();
        for (int i = 0; i < 256; ++i) {
            const std::uint64_t c = result.counts[static_cast<std::size_t>(i)];
            if (c == 0) {
                continue;
            }
            const double p = static_cast<double>(c) / static_cast<double>(length);
            lines << QStringLiteral("%1: %2 ( %3% )")
                         .arg(QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper())
                         .arg(c)
                         .arg(p * 100.0, 0, 'f', 4);
        }

        auto* dialog = new QDialog(this);
        dialog->setWindowTitle(tr("Byte statistics"));
        auto* layout = new QVBoxLayout(dialog);
        auto* text = new QPlainTextEdit(dialog);
        text->setReadOnly(true);
        text->setPlainText(lines.join(QStringLiteral("\n")));
        layout->addWidget(text);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog->setLayout(layout);
        dialog->resize(480, 640);
        dialog->show();
    });

    HexDocument* doc = doc_.get();
    watcher->setFuture(QtConcurrent::run([doc, chunks]() -> StatsResult {
        auto mapFunc = [doc](const std::pair<std::uint64_t, std::uint64_t>& chunk) -> StatsPartial {
            StatsPartial partial;
            QByteArray data;
            QString error;
            if (!doc->readBytes(chunk.first, chunk.second, data, error)) {
                partial.ok = false;
                partial.error = error;
                return partial;
            }
            for (unsigned char b : data) {
                partial.counts[b]++;
            }
            return partial;
        };

        auto reduceFunc = [](StatsResult& aggregate, const StatsPartial& partial) {
            if (!aggregate.ok || !partial.ok) {
                if (!partial.ok && aggregate.ok) {
                    aggregate.ok = false;
                    aggregate.error = partial.error;
                }
                return;
            }
            for (std::size_t i = 0; i < aggregate.counts.size(); ++i) {
                aggregate.counts[i] += partial.counts[i];
            }
        };

        StatsResult aggregate;
        aggregate = QtConcurrent::blockingMappedReduced(chunks, mapFunc, reduceFunc, QtConcurrent::UnorderedReduce);
        return aggregate;
    }));
    statusBar()->showMessage(tr("Computing byte stats…"), 2000);
}

void HexEditorWindow::addBookmark() {
    if (!view_) {
        return;
    }
    const std::uint64_t offset = view_->cursorOffset();
    bool ok = false;
    const QString label =
        QInputDialog::getText(this, tr("Add bookmark"), tr("Label (optional):"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    bookmarks_.push_back(Bookmark{offset, label});
    std::sort(bookmarks_.begin(), bookmarks_.end(),
              [](const Bookmark& a, const Bookmark& b) { return a.offset < b.offset; });
    statusBar()->showMessage(tr("Bookmarked 0x%1").arg(offset, 0, 16), 2000);
    updateActionStates(view_ && view_->selection().has_value());
}

void HexEditorWindow::jumpBookmark(bool forward) {
    if (bookmarks_.empty() || !view_) {
        return;
    }
    const std::uint64_t cur = view_->cursorOffset();
    if (forward) {
        for (const auto& bm : bookmarks_) {
            if (bm.offset > cur) {
                view_->clearSelection();
                view_->setCursorOffset(bm.offset);
                statusBar()->showMessage(
                    tr("Jumped to bookmark: %1 (0x%2)").arg(bm.label, QString::number(bm.offset, 16)));
                return;
            }
        }
        view_->clearSelection();
        view_->setCursorOffset(bookmarks_.front().offset);
        statusBar()->showMessage(tr("Wrapped to first bookmark: %1").arg(bookmarks_.front().label));
    }
    else {
        for (auto it = bookmarks_.rbegin(); it != bookmarks_.rend(); ++it) {
            if (it->offset < cur) {
                view_->clearSelection();
                view_->setCursorOffset(it->offset);
                statusBar()->showMessage(
                    tr("Jumped to bookmark: %1 (0x%2)").arg(it->label, QString::number(it->offset, 16)));
                return;
            }
        }
        view_->clearSelection();
        view_->setCursorOffset(bookmarks_.back().offset);
        statusBar()->showMessage(tr("Wrapped to last bookmark: %1").arg(bookmarks_.back().label));
    }
}

void HexEditorWindow::listBookmarks() {
    if (bookmarks_.empty()) {
        QMessageBox::information(this, tr("Bookmarks"), tr("No bookmarks set."));
        return;
    }
    QStringList lines;
    for (const auto& bm : bookmarks_) {
        lines << tr("0x%1 (%2): %3").arg(bm.offset, 0, 16).arg(bm.offset).arg(bm.label);
    }
    QMessageBox::information(this, tr("Bookmarks"), lines.join(QStringLiteral("\n")));
}

void HexEditorWindow::diffWithFile() {
    if (!doc_) {
        return;
    }
    const QString otherPath = QFileDialog::getOpenFileName(this, tr("Select file to diff"),
                                                           doc_->path().isEmpty() ? QString() : doc_->path());
    if (otherPath.isEmpty()) {
        return;
    }

    struct DiffResult {
        bool ok = true;
        QString error;
        std::vector<std::uint64_t> diffs;
    };

    auto* watcher = new QFutureWatcher<DiffResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        const auto result = watcher->future().result();
        watcher->deleteLater();
        if (!result.ok) {
            QMessageBox::warning(this, tr("Diff"), result.error);
            return;
        }
        diffOffsets_ = std::move(result.diffs);
        currentDiffIndex_ = diffOffsets_.empty() ? -1 : 0;

        if (diffOffsets_.empty()) {
            QMessageBox::information(this, tr("Diff"), tr("Files are identical."));
        }
        else {
            QMessageBox::information(this, tr("Diff"), tr("Found %1 differing byte(s).").arg(diffOffsets_.size()));
            nextDiff(true);
        }
        updateActionStates(view_ && view_->selection().has_value());
    });

    HexDocument* doc = doc_.get();
    watcher->setFuture(QtConcurrent::run([doc, otherPath]() -> DiffResult {
        DiffResult result;
        QFile other(otherPath);
        if (!other.open(QIODevice::ReadOnly)) {
            result.ok = false;
            result.error = QObject::tr("Cannot open %1").arg(otherPath);
            return result;
        }

        const std::uint64_t len = doc->size();
        const std::uint64_t otherLen = static_cast<std::uint64_t>(other.size());
        const std::uint64_t maxLen = std::max<std::uint64_t>(len, otherLen);

        constexpr std::uint64_t kChunk = 256 * 1024;
        std::uint64_t pos = 0;
        QString error;

        while (pos < maxLen) {
            const std::uint64_t chunk = std::min<std::uint64_t>(kChunk, maxLen - pos);
            QByteArray left;
            if (!doc->readBytes(pos, chunk, left, error)) {
                result.ok = false;
                result.error = error;
                return result;
            }
            QByteArray right = other.read(static_cast<qint64>(chunk));
            if (right.size() < static_cast<int>(chunk)) {
                right.resize(static_cast<int>(chunk));
            }
            for (std::uint64_t i = 0; i < chunk; ++i) {
                const bool diff = left.size() > static_cast<int>(i)
                                      ? (static_cast<unsigned char>(left.at(static_cast<int>(i))) !=
                                         static_cast<unsigned char>(right.at(static_cast<int>(i))))
                                      : (static_cast<unsigned char>(right.at(static_cast<int>(i))) != 0);
                if (diff) {
                    result.diffs.push_back(pos + i);
                }
            }
            pos += chunk;
        }
        result.ok = true;
        return result;
    }));
    statusBar()->showMessage(tr("Diffing files…"), 2000);
}

void HexEditorWindow::sideBySideDiff() {
    if (!doc_) {
        return;
    }
    const QString otherPath = QFileDialog::getOpenFileName(this, tr("Select file to diff"),
                                                           doc_->path().isEmpty() ? QString() : doc_->path());
    if (otherPath.isEmpty()) {
        return;
    }

    QFile other(otherPath);
    if (!other.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Diff"), tr("Cannot open %1").arg(otherPath));
        return;
    }

    const std::uint64_t len = doc_->size();
    const std::uint64_t otherLen = static_cast<std::uint64_t>(other.size());
    const std::uint64_t maxLen = std::max<std::uint64_t>(len, otherLen);
    constexpr int kRowBytes = 16;

    auto formatLine = [&](std::uint64_t offsetVal, const QByteArray& data, const QByteArray& otherData) -> QString {
        QString line = QStringLiteral("0x%1 ").arg(offsetVal, 8, 16, QLatin1Char('0')).toUpper();
        QString ascii;
        for (int i = 0; i < kRowBytes; ++i) {
            const bool hasLeft = i < data.size();
            const bool hasRight = i < otherData.size();
            const unsigned char l = hasLeft ? static_cast<unsigned char>(data.at(i)) : 0;
            const unsigned char r = hasRight ? static_cast<unsigned char>(otherData.at(i)) : 0;
            const bool diff = (!hasLeft && hasRight) || (!hasRight && hasLeft) || (l != r);
            if (diff) {
                line += QStringLiteral("!%1").arg(
                    hasLeft ? QString::number(l, 16).rightJustified(2, QLatin1Char('0')).toUpper()
                            : QStringLiteral("--"));
            }
            else {
                line += QStringLiteral(" %1").arg(QString::number(l, 16).rightJustified(2, QLatin1Char('0')).toUpper());
            }
            if ((i % 4) == 3) {
                line += QLatin1Char(' ');
            }
            const char c = hasLeft ? static_cast<char>(l) : ' ';
            ascii +=
                (std::isprint(static_cast<unsigned char>(c)) ? QString(QChar::fromLatin1(c)) : QStringLiteral("."));
        }
        line = line.leftJustified(3 * kRowBytes + 10, QLatin1Char(' '));
        line += QStringLiteral("| %1").arg(ascii);
        return line;
    };

    QStringList leftLines;
    QStringList rightLines;
    std::uint64_t offset = 0;
    while (offset < maxLen) {
        const std::uint64_t toRead = std::min<std::uint64_t>(static_cast<std::uint64_t>(kRowBytes), maxLen - offset);
        QString err;
        QByteArray left;
        if (!doc_->readBytes(offset, toRead, left, err)) {
            QMessageBox::warning(this, tr("Diff"), err);
            return;
        }
        QByteArray right = other.read(static_cast<qint64>(toRead));
        leftLines << formatLine(offset, left, right);
        rightLines << formatLine(offset, right, left);
        offset += toRead;
    }

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Side-by-side diff vs %1").arg(QFileInfo(otherPath).fileName()));
    auto* layout = new QHBoxLayout(dialog);
    auto makePane = [&](const QString& title, const QStringList& lines) {
        auto* pane = new QPlainTextEdit(dialog);
        pane->setReadOnly(true);
        pane->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        pane->setPlainText(lines.join(QStringLiteral("\n")));
        pane->setMinimumWidth(400);
        pane->setLineWrapMode(QPlainTextEdit::NoWrap);
        pane->setProperty("paneTitle", title);
        return pane;
    };
    layout->addWidget(makePane(tr("Current"), leftLines));
    layout->addWidget(makePane(tr("Other"), rightLines));
    dialog->setLayout(layout);
    dialog->resize(900, 600);
    dialog->show();
}

void HexEditorWindow::nextDiff(bool forward) {
    if (diffOffsets_.empty() || !view_) {
        return;
    }
    if (currentDiffIndex_ < 0) {
        currentDiffIndex_ = 0;
    }
    if (forward) {
        currentDiffIndex_ = (currentDiffIndex_ + 1) % static_cast<int>(diffOffsets_.size());
    }
    else {
        currentDiffIndex_ =
            (currentDiffIndex_ - 1 + static_cast<int>(diffOffsets_.size())) % static_cast<int>(diffOffsets_.size());
    }
    const std::uint64_t offset = diffOffsets_[static_cast<std::size_t>(currentDiffIndex_)];
    view_->clearSelection();
    view_->setCursorOffset(offset);
    statusBar()->showMessage(
        tr("Diff %1/%2 at 0x%3").arg(currentDiffIndex_ + 1).arg(diffOffsets_.size()).arg(offset, 0, 16));
}

void HexEditorWindow::updateInspector(std::uint64_t offset) {
    if (!doc_ || !inspectorDock_) {
        return;
    }

    auto setText = [](QLabel* label, const QString& text) {
        if (label) {
            label->setText(text);
        }
    };

    QString error;
    QByteArray data;
    if (!doc_->readBytes(offset, 16, data, error)) {
        setText(inspectorOffset_, tr("N/A"));
        setText(inspectorU8_, tr("N/A"));
        setText(inspectorI8_, tr("N/A"));
        setText(inspectorU16LE_, tr("N/A"));
        setText(inspectorU16BE_, tr("N/A"));
        setText(inspectorU32LE_, tr("N/A"));
        setText(inspectorU32BE_, tr("N/A"));
        setText(inspectorU64LE_, tr("N/A"));
        setText(inspectorU64BE_, tr("N/A"));
        setText(inspectorFloat_, tr("N/A"));
        setText(inspectorDouble_, tr("N/A"));
        setText(inspectorUtf8_, tr("N/A"));
        return;
    }

    setText(inspectorOffset_, tr("0x%1 / %2").arg(offset, 0, 16).arg(offset));

    auto readLE = [&](int bytes, quint64& out) -> bool {
        if (data.size() < bytes) {
            return false;
        }
        out = 0;
        for (int i = bytes - 1; i >= 0; --i) {
            out = (out << 8) | static_cast<unsigned char>(data.at(i));
        }
        return true;
    };
    auto readBE = [&](int bytes, quint64& out) -> bool {
        if (data.size() < bytes) {
            return false;
        }
        out = 0;
        for (int i = 0; i < bytes; ++i) {
            out = (out << 8) | static_cast<unsigned char>(data.at(i));
        }
        return true;
    };

    if (!data.isEmpty()) {
        const quint8 u8 = static_cast<quint8>(data.at(0));
        const qint8 i8 = static_cast<qint8>(data.at(0));
        setText(inspectorU8_, tr("%1 (0x%2)").arg(u8).arg(u8, 0, 16));
        setText(inspectorI8_, QString::number(i8));
    }
    else {
        setText(inspectorU8_, tr("N/A"));
        setText(inspectorI8_, tr("N/A"));
    }

    quint64 v = 0;
    setText(inspectorU16LE_, readLE(2, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU16BE_, readBE(2, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU32LE_, readLE(4, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU32BE_, readBE(4, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU64LE_, readLE(8, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU64BE_, readBE(8, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));

    if (data.size() >= static_cast<int>(sizeof(float))) {
        float f = 0.0f;
        std::memcpy(&f, data.constData(), sizeof(float));
        setText(inspectorFloat_, QString::number(f));
    }
    else {
        setText(inspectorFloat_, tr("N/A"));
    }

    if (data.size() >= static_cast<int>(sizeof(double))) {
        double d = 0.0;
        std::memcpy(&d, data.constData(), sizeof(double));
        setText(inspectorDouble_, QString::number(d));
    }
    else {
        setText(inspectorDouble_, tr("N/A"));
    }

    QString utf8 = QString::fromUtf8(data);
    if (utf8.size() > 32) {
        utf8 = utf8.left(32) + QStringLiteral("…");
    }
    setText(inspectorUtf8_, utf8);
}

void HexEditorWindow::goToOffset() {
    if (!doc_) {
        return;
    }
    bool ok = false;
    const QString input = QInputDialog::getText(
        this, tr("Go to offset"), tr("Enter offset (decimal or hex, e.g. 0x10):"), QLineEdit::Normal, QString(), &ok);
    if (!ok || input.isEmpty()) {
        return;
    }
    QString trimmed = input.trimmed();
    int base = 10;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        base = 16;
        trimmed = trimmed.mid(2);
    }
    bool parseOk = false;
    const std::uint64_t offset = trimmed.toULongLong(&parseOk, base);
    if (!parseOk) {
        QMessageBox::warning(this, tr("Go to offset"), tr("Invalid number."));
        return;
    }
    const std::uint64_t target = std::min<std::uint64_t>(offset, doc_->size());
    view_->clearSelection();
    view_->setCursorOffset(target);
}

void HexEditorWindow::closeEvent(QCloseEvent* event) {
    if (!promptToSave()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

}  // namespace PCManFM
