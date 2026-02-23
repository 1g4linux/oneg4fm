/*
 * Capstone-based disassembly viewer
 * src/ui/disassemblywindow.cpp
 */

#include "disassemblywindow.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include "binarydocument.h"
#include "color_delegate.h"
#include "color_manager.h"
#include "disasmmodel.h"

#include <algorithm>
#include <QStringList>

namespace PCManFM {

namespace {
// Avoid loading huge binaries into memory at once; trim to a reasonable preview size.
constexpr qsizetype kMaxBytes = 512 * 1024;  // 512 KiB
}  // namespace

DisassemblyWindow::DisassemblyWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
}

void DisassemblyWindow::setupUi() {
    setWindowTitle(tr("Disassembly"));
    setAttribute(Qt::WA_DeleteOnClose);
    statusBar()->setSizeGripEnabled(false);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    pathLabel_ = new QLabel(central);
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);

    view_ = new QTableView(central);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setWordWrap(false);
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->setAlternatingRowColors(true);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(view_);

    setCentralWidget(central);

    auto* toolbar = addToolBar(tr("Disassembly"));
    toolbar->setMovable(false);

    auto* copyAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy All"));
    connect(copyAction, &QAction::triggered, this, [this] {
        if (model_) {
            QStringList lines;
            const int rows = model_->rowCount();
            for (int r = 0; r < rows; ++r) {
                const QString addr = model_->data(model_->index(r, DisasmModel::Address)).toString();
                const QString bytes = model_->data(model_->index(r, DisasmModel::Bytes)).toString();
                const QString mnem = model_->data(model_->index(r, DisasmModel::Mnemonic)).toString();
                const QString ops = model_->data(model_->index(r, DisasmModel::Operands)).toString();
                lines << QStringLiteral("%1  %2  %3 %4").arg(addr, bytes, mnem, ops).trimmed();
            }
            QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
        }
    });

    auto* reloadAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Reload"));
    connect(reloadAction, &QAction::triggered, this, [this] {
        if (currentPath_.isEmpty()) {
            return;
        }
        QString error;
        if (!openFile(currentPath_, error) && !error.isEmpty()) {
            QMessageBox::warning(this, tr("Disassembly"), error);
        }
    });

    colors_ = std::make_unique<ColorManager>(this);
    delegate_ = new ColorDelegate(colors_.get(), view_);
    view_->setItemDelegate(delegate_);
    connect(colors_.get(), &ColorManager::schemeChanged, view_, [this] {
        if (view_) {
            view_->viewport()->update();
        }
    });
}

bool DisassemblyWindow::openFile(const QString& path, QString& errorOut) {
    errorOut.clear();
    doc_ = std::make_unique<BinaryDocument>();
    if (!doc_->open(path, errorOut)) {
        doc_.reset();
        return false;
    }
    if (doc_->arch() == CpuArch::Unknown || doc_->size() == 0) {
        errorOut = tr("Disassembly is only available for supported executable binaries.");
        doc_.reset();
        return false;
    }

    if (!model_) {
        model_ = std::make_unique<DisasmModel>(this);
        if (view_) {
            view_->setModel(model_.get());
            view_->horizontalHeader()->setSectionResizeMode(DisasmModel::Address, QHeaderView::ResizeToContents);
            view_->horizontalHeader()->setSectionResizeMode(DisasmModel::Bytes, QHeaderView::ResizeToContents);
            view_->horizontalHeader()->setSectionResizeMode(DisasmModel::Mnemonic, QHeaderView::ResizeToContents);
        }
    }

    currentPath_ = path;
    return refresh(0);
}

bool DisassemblyWindow::refresh(quint64 offset) {
    if (!doc_) {
        return false;
    }

    const quint64 safeOffset = std::min<quint64>(offset, doc_->size());
    const quint64 remaining = doc_->size() > safeOffset ? doc_->size() - safeOffset : 0;
    const quint64 length = std::min<quint64>(remaining, kMaxBytes);
    if (length == 0) {
        if (model_) {
            model_->clear();
        }
        updateLabels(currentPath_, false, 0);
        return true;
    }
    QString error;
    if (!model_->disassemble(*doc_, offset, length, error)) {
        QMessageBox::warning(this, tr("Disassembly"), error.isEmpty() ? tr("Failed to disassemble file.") : error);
        return false;
    }

    updateLabels(currentPath_, length == kMaxBytes && doc_->size() > kMaxBytes, static_cast<qsizetype>(length));
    if (view_) {
        view_->scrollToTop();
        if (model_->rowCount() > 0) {
            view_->selectRow(0);
        }
    }
    return true;
}

void DisassemblyWindow::updateLabels(const QString& path, bool truncated, qsizetype bytesRead) {
    if (pathLabel_) {
        pathLabel_->setText(tr("Path: %1").arg(path));
    }

    if (truncated) {
        statusBar()->showMessage(tr("Showing first %1 bytes (truncated).").arg(bytesRead));
    }
    else {
        statusBar()->clearMessage();
    }

    setWindowTitle(tr("Disassembly - %1").arg(QFileInfo(path).fileName()));
}

}  // namespace PCManFM
