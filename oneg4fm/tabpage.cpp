/*
 * Tab page implementation for oneg4fm
 * oneg4fm/tabpage.cpp
 */

#include "tabpage.h"

#include "panel/panel.h"

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

#include "application.h"
#include "launcher.h"
#include "settings.h"

using namespace Panel;

namespace {
// Constants for timer delays to avoid magic numbers
constexpr int kUiUpdateDelay = 10;
constexpr int kSelectionDelay = 200;

// Helper to get settings without repetitive casting
Oneg4FM::Settings& appSettings() {
    return static_cast<Oneg4FM::Application*>(qApp)->settings();
}

// Helper function to format status for a single file.
// Defined here as static to avoid modifying the header file.
QString formatSingleFileStatus(const std::shared_ptr<const Panel::FileInfo>& fi, const Oneg4FM::Settings& settings) {
    bool showRealName = settings.showFullNames() && strcmp(fi->dirPath().uriScheme().get(), "menu") != 0;

    QString name = showRealName ? QString::fromStdString(fi->name()) : fi->displayName();

    QString mimeStr = QString::fromUtf8(fi->mimeType()->desc());
    QString linkTarget;

    if (fi->isSymlink()) {
        linkTarget = QObject::tr("Link to") + QChar(QChar::Space) + QString::fromStdString(fi->target());
    }

    if (fi->isDir()) {
        if (fi->isSymlink()) {
            return QStringLiteral("\"%1\" %2 (%3)").arg(name, mimeStr, linkTarget);
        }
        return QStringLiteral("\"%1\" %2").arg(name, mimeStr);
    }

    // It is a file
    QString sizeStr = Panel::formatFileSize(fi->size(), fm_config->si_unit);

    if (fi->isSymlink()) {
        return QStringLiteral("\"%1\" (%2) %3 (%4)").arg(name, sizeStr, mimeStr, linkTarget);
    }

    return QStringLiteral("\"%1\" (%2) %3").arg(name, sizeStr, mimeStr);
}

}  // namespace

namespace Oneg4FM {

//==================================================
// ProxyFilter Implementation
//==================================================

bool ProxyFilter::filterAcceptsRow(const Panel::ProxyFolderModel* model,
                                   const std::shared_ptr<const Panel::FileInfo>& info) const {
    if (!model || !info) {
        return true;
    }

    // OPTIMIZATION: Fail fast if no filter is set.
    // This avoids expensive QString allocation for every row during normal browsing.
    if (filterStr_.isEmpty()) {
        return true;
    }

    QString baseName = fullName_ && !info->name().empty() ? QString::fromStdString(info->name()) : info->displayName();

    if (!baseName.contains(filterStr_, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

//==================================================
// FilterEdit Implementation
//==================================================

FilterEdit::FilterEdit(QWidget* parent) : QLineEdit(parent) {
    setClearButtonEnabled(true);
    if (QToolButton* clearButton = findChild<QToolButton*>()) {
        clearButton->setToolTip(tr("Clear text (Ctrl+K or Esc)"));
    }
}

void FilterEdit::keyPressEvent(QKeyEvent* event) {
    // since two views can be shown in the split mode, Ctrl+K can't be
    // used as a QShortcut but can come here for clearing the text
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_K) {
        clear();
    }
    QLineEdit::keyPressEvent(event);
}

void FilterEdit::keyPressed(QKeyEvent* event) {
    // NOTE: Movement and delete keys should be left to the view
    // Copy/paste shortcuts are taken by the view but they aren't needed here
    if (!hasFocus() && event->key() != Qt::Key_Left && event->key() != Qt::Key_Right && event->key() != Qt::Key_Home &&
        event->key() != Qt::Key_End && event->key() != Qt::Key_Delete) {
        keyPressEvent(event);
    }
}

//==================================================
// FilterBar Implementation
//==================================================

FilterBar::FilterBar(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* HLayout = new QHBoxLayout(this);
    HLayout->setSpacing(5);
    filterEdit_ = new FilterEdit();
    QLabel* label = new QLabel(tr("Filter:"));
    HLayout->addWidget(label);
    HLayout->addWidget(filterEdit_);
    connect(filterEdit_, &QLineEdit::textChanged, this, &FilterBar::textChanged);
    connect(filterEdit_, &FilterEdit::lostFocus, this, &FilterBar::lostFocus);
}

//==================================================
// TabPage Implementation
//==================================================

TabPage::TabPage(QWidget* parent)
    : QWidget(parent),
      folderView_{nullptr},
      folderModel_{nullptr},
      proxyModel_{nullptr},
      proxyFilter_{nullptr},
      verticalLayout{nullptr},
      overrideCursor_(false),
      selectionTimer_(nullptr),
      filterBar_(nullptr),
      changingDir_(false) {
    Settings& settings = appSettings();

    // create proxy folder model to do item filtering
    proxyModel_ = new ImageMagickProxyFolderModel();
    proxyModel_->setShowHidden(settings.showHidden());
    proxyModel_->setBackupAsHidden(settings.backupAsHidden());
    proxyModel_->setShowThumbnails(settings.showThumbnails());
    proxyModel_->setThumbnailSize(settings.thumbnailIconSize());

    connect(proxyModel_, &ProxyFolderModel::sortFilterChanged, this, [this] {
        QToolTip::showText(QPoint(), QString());  // remove the tooltip, if any
        if (!changingDir_) {
            saveFolderSorting();
            Q_EMIT sortFilterChanged();
        }
    });

    verticalLayout = new QVBoxLayout(this);
    verticalLayout->setContentsMargins(0, 0, 0, 0);

    folderView_ = new View(settings.viewMode(), this);
    folderView_->setMargins(settings.folderViewCellMargins());
    folderView_->setShadowHidden(settings.shadowHidden());

    connect(folderView_, &View::selChanged, this, &TabPage::onSelChanged);
    connect(folderView_, &View::clickedBack, this, &TabPage::backwardRequested);
    connect(folderView_, &View::clickedForward, this, &TabPage::forwardRequested);

    // customization of columns of detailed list view
    folderView_->setCustomColumnWidths(settings.getCustomColumnWidths());
    folderView_->setHiddenColumns(settings.getHiddenColumns());

    connect(folderView_, &View::columnResizedByUser, this,
            [this, &settings]() { settings.setCustomColumnWidths(folderView_->getCustomColumnWidths()); });
    connect(folderView_, &View::columnHiddenByUser, this,
            [this, &settings]() { settings.setHiddenColumns(folderView_->getHiddenColumns()); });

    proxyFilter_ = new ProxyFilter();
    proxyModel_->addFilter(proxyFilter_);

    // attach proxy model to the folder view; FolderModel is set as source model in chdir()
    folderView_->setModel(proxyModel_);
    verticalLayout->addWidget(folderView_);

    if (auto* viewWidget = folderView_->childView()) {
        // Leave breathing room at the bottom so the last row is not clipped.
        viewWidget->viewport()->setContentsMargins(0, 0, 0, 30);
    }

    folderView_->childView()->installEventFilter(this);
    if (settings.noItemTooltip()) {
        folderView_->childView()->viewport()->installEventFilter(this);
    }

    // filter-bar and its settings
    filterBar_ = new FilterBar();
    verticalLayout->addWidget(filterBar_);
    if (!settings.showFilter()) {
        transientFilterBar(true);
    }
    connect(filterBar_, &FilterBar::textChanged, this, &TabPage::onFilterStringChanged);
}

TabPage::~TabPage() {
    freeFolder();

    // NOTE: If TabPage header was updated to use std::unique_ptr, these deletes would be unnecessary.
    // Keeping them for compatibility with existing raw pointer header definitions.
    if (proxyFilter_) {
        delete proxyFilter_;
    }
    if (proxyModel_) {
        delete proxyModel_;
    }

    if (folderModel_) {
        disconnect(folderModel_, &Panel::FolderModel::fileSizeChanged, this, &TabPage::onFileSizeChanged);
        disconnect(folderModel_, &Panel::FolderModel::filesAdded, this, &TabPage::onFilesAdded);
        folderModel_->unref();
    }

    if (overrideCursor_) {
        QApplication::restoreOverrideCursor();  // remove busy cursor
    }
}

void TabPage::transientFilterBar(bool transient) {
    if (filterBar_) {
        filterBar_->clear();
        if (transient) {
            filterBar_->hide();
            connect(filterBar_, &FilterBar::lostFocus, this, &TabPage::onLosingFilterBarFocus);
        }
        else {
            filterBar_->show();
            disconnect(filterBar_, &FilterBar::lostFocus, this, &TabPage::onLosingFilterBarFocus);
        }
    }
}

void TabPage::onLosingFilterBarFocus() {
    // hide the empty transient filter-bar when it loses focus
    if (getFilterStr().isEmpty()) {
        filterBar_->hide();
    }
}

void TabPage::showFilterBar() {
    if (filterBar_) {
        filterBar_->show();
        if (isVisibleTo(this)) {  // the page itself may be in an inactive tab
            filterBar_->focusBar();
        }
    }
}

bool TabPage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == folderView_->childView() && event->type() == QEvent::KeyPress) {
        QToolTip::showText(QPoint(), QString());  // remove the tooltip, if any

        if (QKeyEvent* ke = static_cast<QKeyEvent*>(event)) {
            if (filterBar_ && !appSettings().showFilter()) {
                // With a transient filter-bar, transfer the pressed keys to the bar
                if (ke->key() == Qt::Key_Backspace && ke->modifiers() == Qt::NoModifier && !filterBar_->isVisible()) {
                    if (!ke->isAutoRepeat()) {
                        Q_EMIT backspacePressed();
                    }
                }
                else if (ke->key() != Qt::Key_Delete) {
                    // Don't intercept Delete key - let it be handled by the main window action
                    filterBar_->keyPressed(ke);
                }
            }
            else if (ke->key() == Qt::Key_Backspace && ke->modifiers() == Qt::NoModifier) {
                Q_EMIT backspacePressed();
            }
        }
    }
    else if (watched == folderView_->childView()->viewport() && event->type() == QEvent::ToolTip) {
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void TabPage::onFilterStringChanged(QString str) {
    if (filterBar_ && str != getFilterStr()) {
        QToolTip::showText(QPoint(), QString());  // remove the tooltip, if any

        bool transientFilterBar = !appSettings().showFilter();

        // with a transient filter-bar, let the current index be selected by Qt
        // if the first pressed key is a space
        if (transientFilterBar && !filterBar_->isVisibleTo(this) && folderView()->childView()->hasFocus() &&
            str == QString(QChar(QChar::Space))) {
            QModelIndex index = folderView_->selectionModel()->currentIndex();
            if (index.isValid() && !folderView_->selectionModel()->isSelected(index)) {
                filterBar_->clear();
                folderView_->childView()->scrollTo(index, QAbstractItemView::EnsureVisible);
                return;
            }
        }

        setFilterStr(str);

        // Because the filter string may be typed inside the view, we should wait
        // for Qt to select an item before deciding about the selection in
        // applyFilter() Therefore, we use a single-shot timer to apply the filter
        QTimer::singleShot(0, folderView_, [this] { applyFilter(); });

        // show/hide the transient filter-bar appropriately
        if (transientFilterBar) {
            if (filterBar_->isVisibleTo(this)) {
                if (str.isEmpty()) {
                    // focus the view BEFORE hiding the filter-bar
                    folderView()->childView()->setFocus();
                    filterBar_->hide();
                }
            }
            else if (!str.isEmpty()) {
                filterBar_->show();
            }
        }
    }
}

void TabPage::freeFolder() {
    if (folder_) {
        if (folderSettings_.isCustomized()) {
            // save custom view settings for this folder
            appSettings().saveFolderSettings(folder_->path(), folderSettings_);
        }
        disconnect(folder_.get(), nullptr, this, nullptr);  // disconnect from all signals
        folder_ = nullptr;
        filesToTrust_.clear();
    }
}

QScrollBar* TabPage::historyScrollBar() const {
    if (!folderView_) {
        return nullptr;
    }
    auto* itemView = folderView_->childView();
    if (!itemView) {
        return nullptr;
    }
    if (folderView_->viewMode() == Panel::FolderView::CompactMode) {
        return itemView->horizontalScrollBar();
    }
    return itemView->verticalScrollBar();
}

int TabPage::currentHistoryScrollPos() const {
    if (QScrollBar* sbar = historyScrollBar()) {
        return sbar->value();
    }
    return 0;
}

void TabPage::saveCurrentHistoryScrollPos() {
    if (history_.size() == 0) {
        return;
    }
    history_.currentItem().setScrollPos(currentHistoryScrollPos());
}

void TabPage::restoreCurrentHistoryScrollPos() {
    if (history_.size() == 0) {
        return;
    }
    if (QScrollBar* sbar = historyScrollBar()) {
        sbar->setValue(browseHistory().currentScrollPos());
    }
}

void TabPage::onFolderStartLoading() {
    if (folderModel_) {
        disconnect(folderModel_, &Panel::FolderModel::filesAdded, this, &TabPage::onFilesAdded);
    }
    if (!overrideCursor_) {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        overrideCursor_ = true;
    }
}

void TabPage::onUiUpdated() {
    bool scrolled = false;
    // if there are files to select, select them
    if (!filesToSelect_.empty()) {
        Panel::FileInfoList infos;
        for (const auto& file : filesToSelect_) {
            if (auto info = proxyModel_->fileInfoFromPath(file)) {
                infos.push_back(info);
            }
        }
        filesToSelect_.clear();
        if (folderView_->selectFiles(infos)) {
            scrolled = true;  // scrolling is done by FolderView::selectFiles()
            QModelIndexList indexes = folderView_->selectionModel()->selectedIndexes();
            if (!indexes.isEmpty()) {
                folderView_->selectionModel()->setCurrentIndex(indexes.first(), QItemSelectionModel::NoUpdate);
            }
        }
    }

    // if the current folder is the parent folder of the last browsed folder,
    // select the folder item in current view
    if (!scrolled && lastFolderPath_ && lastFolderPath_.parent() == path()) {
        QModelIndex index = folderView_->indexFromFolderPath(lastFolderPath_);
        if (index.isValid()) {
            folderView_->childView()->scrollTo(index, QAbstractItemView::EnsureVisible);
            folderView_->childView()->setCurrentIndex(index);
            scrolled = true;
        }
    }

    if (!scrolled) {
        // set the first item as current
        QModelIndex firstIndx = proxyModel_->index(0, 0);
        if (firstIndx.isValid()) {
            folderView_->selectionModel()->setCurrentIndex(firstIndx, QItemSelectionModel::NoUpdate);
        }
        // scroll to recorded position
        restoreCurrentHistoryScrollPos();
    }

    if (folderModel_) {
        // update selection statusbar info when needed
        connect(folderModel_, &Panel::FolderModel::fileSizeChanged, this, &TabPage::onFileSizeChanged);
        // get ready to select files that may be added later
        connect(folderModel_, &Panel::FolderModel::filesAdded, this, &TabPage::onFilesAdded);
    }

    // in the single-click mode, set the cursor shape of the view to a pointing
    // hand only if there is an item under it
    if (folderView_->style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick)) {
        QTimer::singleShot(0, folderView_, [this] {
            QPoint pos = folderView_->childView()->mapFromGlobal(QCursor::pos());
            QModelIndex index = folderView_->childView()->indexAt(pos);
            if (index.isValid()) {
                folderView_->setCursor(Qt::PointingHandCursor);
            }
            else {
                folderView_->setCursor(Qt::ArrowCursor);
            }
        });
    }
}

void TabPage::onFileSizeChanged(const QModelIndex& index) {
    if (folderView_->hasSelection()) {
        QModelIndexList indexes = folderView_->selectionModel()->selectedIndexes();
        if (indexes.contains(proxyModel_->mapFromSource(index))) {
            onSelChanged();
        }
    }
}

// slot
void TabPage::onFilesAdded(Panel::FileInfoList files) {
    if (appSettings().selectNewFiles()) {
        if (!selectionTimer_) {
            selectionTimer_ = new QTimer(this);
            selectionTimer_->setSingleShot(true);
            if (folderView_->selectFiles(files, false)) {
                selectionTimer_->start(kSelectionDelay);
            }
        }
        else if (folderView_->selectFiles(files, selectionTimer_->isActive())) {
            selectionTimer_->start(kSelectionDelay);
        }
    }
    else if (!folderView_->selectionModel()->currentIndex().isValid()) {
        // set the first item as current if there is no current item
        QModelIndex firstIndx = proxyModel_->index(0, 0);
        if (firstIndx.isValid()) {
            folderView_->selectionModel()->setCurrentIndex(firstIndx, QItemSelectionModel::NoUpdate);
        }
    }

    // trust the files that are added by createShortcut()
    if (!filesToTrust_.isEmpty()) {
        for (const auto& file : files) {
            const QString fileName = QString::fromStdString(file->name());
            if (filesToTrust_.contains(fileName)) {
                file->setTrustable(true);
                filesToTrust_.removeAll(fileName);
                if (filesToTrust_.isEmpty()) {
                    break;
                }
            }
        }
    }
}

void TabPage::localizeTitle(const Panel::FilePath& path) {
    // force localization for some virtual locations represented by libfm-qt URIs
    if (!path.isNative()) {
        if (path.hasUriScheme("search")) {
            title_ = tr("Search Results");
        }
        else if (strcmp(path.toString().get(), "menu://applications/") == 0) {
            title_ = tr("Applications");
        }
        else if (!path.hasParent()) {
            if (path.hasUriScheme("trash")) {
                title_ = tr("Trash");
            }
        }
    }
    else if (QString::fromUtf8(path.toString().get()) ==
             QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)) {
        title_ = tr("Desktop");
    }
}

void TabPage::onFolderFinishLoading() {
    auto fi = folder_->info();
    if (fi) {
        title_ = fi->displayName();
        localizeTitle(folder_->path());
        Q_EMIT titleChanged();
    }

    // query filesystem info so free space and total size can be shown and kept up to date
    folder_->queryFilesystemInfo();

    // update status text
    QString& text = statusText_[StatusTextNormal];
    text = formatStatusText();
    Q_EMIT statusChanged(StatusTextNormal, text);

    if (overrideCursor_) {
        QApplication::restoreOverrideCursor();  // remove busy cursor
        overrideCursor_ = false;
    }

    // After finishing loading the folder, the model is updated, but Qt delays the
    // UI update for performance reasons. We use a singleShot to wait for it.
    QTimer::singleShot(kUiUpdateDelay, this, &TabPage::onUiUpdated);
}

void TabPage::onFolderError(const Panel::GErrorPtr& err,
                            Panel::Job::ErrorSeverity severity,
                            Panel::Job::ErrorAction& response) {
    // Only show more severe errors to the users and ignore milder errors.
    if (folder_ && severity >= Panel::Job::ErrorSeverity::MODERATE) {
        QMessageBox::critical(this, tr("Error"), err.message());
    }

    response = Panel::Job::ErrorAction::CONTINUE;
}

void TabPage::onFolderFsInfo() {
    guint64 free, total;
    QString& msg = statusText_[StatusTextFSInfo];
    if (folder_->getFilesystemInfo(&total, &free)) {
        msg = tr("Free space: %1 (Total: %2)")
                  .arg(formatFileSize(free, fm_config->si_unit), formatFileSize(total, fm_config->si_unit));
    }
    else {
        msg.clear();
    }
    Q_EMIT statusChanged(StatusTextFSInfo, msg);
}

QString TabPage::formatStatusText() {
    if (!proxyModel_ || !folder_) {
        return QString();
    }

    int shown_files = proxyModel_->rowCount();
    int total_files = folderModel_ ? folderModel_->rowCount() : folder_->files().size();
    int hidden_files = qMax(0, total_files - shown_files);

    QString text = tr("%n item(s)", "", shown_files);
    if (hidden_files > 0) {
        text += tr(" (%n hidden)", "", hidden_files);
    }

    auto fi = folder_->info();
    if (fi && fi->isSymlink()) {
        text += QStringLiteral(" (%1)").arg(tr("Link to") + QChar(QChar::Space) + QString::fromStdString(fi->target()));
    }

    return text;
}

void TabPage::onFolderRemoved() {
    // the folder we're showing is removed, destroy the widget
    qDebug("folder removed");
    chdir(Panel::FilePath::homeDir());
}

void TabPage::onFolderUnmount() {
    // the folder we're showing is unmounted, destroy the widget
    qDebug("folder unmount");

    if (folder_) {
        // the folder shouldn't be freed here because the dir will be changed by
        // the slot of MainWindow but it should be disconnected from all signals
        disconnect(folder_.get(), nullptr, this, nullptr);
    }
    Q_EMIT folderUnmounted();
}

void TabPage::onFolderContentChanged() {
    /* update status text */
    statusText_[StatusTextNormal] = formatStatusText();
    Q_EMIT statusChanged(StatusTextNormal, statusText_[StatusTextNormal]);
}

QString TabPage::pathName() {
    auto filePath = path();
    if (!filePath) {
        return QString();
    }
    auto dispPath = filePath.toString();
    return QString::fromUtf8(dispPath.get());
}

void TabPage::chdir(Panel::FilePath newPath, bool addHistory) {
    if (filterBar_) {
        filterBar_->clear();
    }
    if (folder_) {
        // we're already in the specified dir
        if (newPath == folder_->path()) {
            return;
        }

        if (newPath.hasUriScheme("admin")) {
            QMessageBox::warning(this, tr("Admin mode disabled"),
                                 tr("Admin locations are not supported in this build."));
            return;
        }

        // reset the status selected text
        statusText_[StatusTextSelectedFiles] = QString();

        // remember the previous folder path that we have browsed
        lastFolderPath_ = folder_->path();

        if (addHistory) {
            // store current scroll pos in the browse history
            saveCurrentHistoryScrollPos();
        }

        // free the previous model
        if (folderModel_) {
            disconnect(folderModel_, &Panel::FolderModel::fileSizeChanged, this, &TabPage::onFileSizeChanged);
            disconnect(folderModel_, &Panel::FolderModel::filesAdded, this, &TabPage::onFilesAdded);
            proxyModel_->setSourceModel(nullptr);
            folderModel_->unref();  // unref the cached model
            folderModel_ = nullptr;
        }

        freeFolder();
    }

    changingDir_ = true;

    // remove the tooltip, if any
    QToolTip::showText(QPoint(), QString());
    // set title as with path button (will change if the new folder is loaded)
    title_ = QString::fromUtf8(newPath.baseName().get());
    localizeTitle(newPath);
    Q_EMIT titleChanged();

    folder_ = Panel::Folder::fromPath(newPath);
    if (addHistory) {
        // add current path to browse history
        history_.add(path());
    }
    connect(folder_.get(), &Panel::Folder::startLoading, this, &TabPage::onFolderStartLoading);
    connect(folder_.get(), &Panel::Folder::finishLoading, this, &TabPage::onFolderFinishLoading);
    connect(folder_.get(), &Panel::Folder::error, this, &TabPage::onFolderError);
    connect(folder_.get(), &Panel::Folder::fileSystemChanged, this, &TabPage::onFolderFsInfo);
    connect(folder_.get(), &Panel::Folder::removed, this, &TabPage::onFolderRemoved);
    connect(folder_.get(), &Panel::Folder::unmount, this, &TabPage::onFolderUnmount);
    connect(folder_.get(), &Panel::Folder::contentChanged, this, &TabPage::onFolderContentChanged);

    Settings& settings = appSettings();
    folderModel_ = CachedFolderModel::modelFromFolder(folder_);

    bool forceShortNames = !settings.showFullNames() || newPath.hasUriScheme("menu") || newPath.hasUriScheme("trash");

    folderModel_->setShowFullName(!forceShortNames);
    proxyFilter_->filterFullName(!forceShortNames);

    folderSettings_ = settings.loadFolderSettings(path());

    // set sorting
    proxyModel_->sort(folderSettings_.sortColumn(), folderSettings_.sortOrder());
    proxyModel_->setFolderFirst(folderSettings_.sortFolderFirst());
    proxyModel_->setShowHidden(folderSettings_.showHidden());
    proxyModel_->setSortCaseSensitivity(folderSettings_.sortCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive);
    proxyModel_->setSourceModel(folderModel_);

    // set view mode
    setViewMode(folderSettings_.viewMode());

    if (folder_->isLoaded()) {
        onFolderStartLoading();
        onFolderFinishLoading();
        onFolderFsInfo();
    }
    else {
        onFolderStartLoading();
    }

    changingDir_ = false;
}

void TabPage::selectAll() {
    folderView_->selectAll();
}

void TabPage::deselectAll() {
    folderView_->selectionModel()->clearSelection();
}

void TabPage::invertSelection() {
    folderView_->invertSelection();
}

void TabPage::reload() {
    if (folder_) {
        // don't select or scroll to the previous folder after reload
        lastFolderPath_ = Panel::FilePath();

        // but remember the current scroll position
        saveCurrentHistoryScrollPos();

        folder_->reload();
    }
}

// when the current selection in the folder view is changed
void TabPage::onSelChanged() {
    QString msg;

    if (!folderView_->hasSelection()) {
        statusText_[StatusTextSelectedFiles] = QString();
        Q_EMIT statusChanged(StatusTextSelectedFiles, QString());
        return;
    }

    auto files = folderView_->selectedFiles();
    int numSel = files.size();

    if (numSel == 1) {
        /* only one file is selected (delegated to helper function) */
        msg = formatSingleFileStatus(files.front(), appSettings());
    }
    else {
        goffset sum;
        msg = tr("%n item(s) selected", nullptr, numSel);

        // show total size only when the number of selected entries is reasonable
        if (numSel < 1000) {
            sum = 0;
            bool dirFound = false;
            for (auto& fi : files) {
                if (fi->isDir()) {
                    dirFound = true;
                    break;
                }
                sum += fi->size();
            }
            if (!dirFound) {
                msg += QStringLiteral(" (%1)").arg(Panel::formatFileSize(sum, fm_config->si_unit));
            }
        }
    }

    statusText_[StatusTextSelectedFiles] = msg;
    Q_EMIT statusChanged(StatusTextSelectedFiles, msg);
}

void TabPage::backward() {
    saveCurrentHistoryScrollPos();

    history_.backward();
    chdir(history_.currentPath(), false);
}

void TabPage::forward() {
    saveCurrentHistoryScrollPos();

    history_.forward();
    chdir(history_.currentPath(), false);
}

void TabPage::jumpToHistory(int index) {
    if (index >= 0 && static_cast<size_t>(index) < history_.size()) {
        saveCurrentHistoryScrollPos();

        history_.setCurrentIndex(index);
        chdir(history_.currentPath(), false);
    }
}

bool TabPage::canUp() {
    auto _path = path();
    return (_path && _path.hasParent());
}

void TabPage::up() {
    auto _path = path();
    if (_path) {
        auto parent = _path.parent();
        if (parent) {
            chdir(parent, true);
        }
    }
}

void TabPage::updateFromSettings(Settings& settings) {
    folderView_->updateFromSettings(settings);

    auto* viewport = folderView_->childView()->viewport();
    viewport->removeEventFilter(this);

    if (settings.noItemTooltip()) {
        viewport->installEventFilter(this);
    }
}

void TabPage::setViewMode(Panel::FolderView::ViewMode mode) {
    Settings& settings = appSettings();
    if (folderSettings_.viewMode() != mode) {
        folderSettings_.setViewMode(mode);
        if (!changingDir_ && folderSettings_.isCustomized()) {
            settings.saveFolderSettings(path(), folderSettings_);
        }
    }
    Panel::FolderView::ViewMode prevMode = folderView_->viewMode();
    folderView_->setViewMode(mode);
    if (folderView_->isVisible()) {  // in the current tab
        folderView_->childView()->setFocus();
    }
    if (prevMode != folderView_->viewMode()) {
        // Re-install event filter after view mode change
        folderView_->childView()->removeEventFilter(this);
        folderView_->childView()->installEventFilter(this);
        if (settings.noItemTooltip()) {
            folderView_->childView()->viewport()->removeEventFilter(this);
            folderView_->childView()->viewport()->installEventFilter(this);
        }
        onSelChanged();
    }
}

void TabPage::sort(int col, Qt::SortOrder order) {
    if (proxyModel_) {
        proxyModel_->sort(col, order);
    }
}

void TabPage::setSortFolderFirst(bool value) {
    if (proxyModel_) {
        proxyModel_->setFolderFirst(value);
    }
}

void TabPage::setSortCaseSensitive(bool value) {
    if (proxyModel_) {
        proxyModel_->setSortCaseSensitivity(value ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }
}

void TabPage::setShowHidden(bool showHidden) {
    if (proxyModel_) {
        proxyModel_->setShowHidden(showHidden);
    }
}

void TabPage::setShowThumbnails(bool showThumbnails) {
    Settings& settings = appSettings();
    settings.setShowThumbnails(showThumbnails);
    if (proxyModel_) {
        proxyModel_->setShowThumbnails(showThumbnails);
    }
}

void TabPage::saveFolderSorting() {
    if (proxyModel_ == nullptr) {
        return;
    }
    folderSettings_.setSortOrder(proxyModel_->sortOrder());
    int sortCol = proxyModel_->sortColumn();
    Panel::FolderModel::ColumnId columnId = (sortCol >= 0 && sortCol < Panel::FolderModel::NumOfColumns)
                                                ? static_cast<Panel::FolderModel::ColumnId>(sortCol)
                                                : Panel::FolderModel::ColumnFileName;
    folderSettings_.setSortColumn(columnId);
    folderSettings_.setSortFolderFirst(proxyModel_->folderFirst());
    folderSettings_.setSortCaseSensitive(proxyModel_->sortCaseSensitivity());

    if (folderSettings_.showHidden() != proxyModel_->showHidden()) {
        folderSettings_.setShowHidden(proxyModel_->showHidden());
        statusText_[StatusTextNormal] = formatStatusText();
        Q_EMIT statusChanged(StatusTextNormal, statusText_[StatusTextNormal]);
    }
    if (folderSettings_.isCustomized()) {
        appSettings().saveFolderSettings(path(), folderSettings_);
    }
}

void TabPage::applyFilter() {
    if (proxyModel_ == nullptr) {
        return;
    }

    int prevSelSize = folderView_->selectionModel()->selectedIndexes().size();

    proxyModel_->updateFilters();

    QModelIndex firstIndx = proxyModel_->index(0, 0);

    if (proxyFilter_->getFilterStr().isEmpty()) {
        QModelIndex curIndex = folderView_->selectionModel()->currentIndex();
        if (curIndex.isValid()) {
            folderView_->childView()->scrollTo(curIndex, QAbstractItemView::EnsureVisible);
        }
        else if (firstIndx.isValid()) {
            folderView_->selectionModel()->setCurrentIndex(firstIndx, QItemSelectionModel::NoUpdate);
        }
    }
    else {
        bool selectionMade = false;

        // preselect an appropriate item if the filter-bar is transient
        if (firstIndx.isValid() && !appSettings().showFilter()) {
            auto indexList = proxyModel_->match(firstIndx, Qt::DisplayRole, proxyFilter_->getFilterStr());
            if (!indexList.isEmpty()) {
                if (!folderView_->selectionModel()->isSelected(indexList.at(0))) {
                    folderView_->childView()->setCurrentIndex(indexList.at(0));
                    selectionMade = true;
                }
            }
            else if (!folderView_->selectionModel()->isSelected(firstIndx)) {
                folderView_->childView()->setCurrentIndex(firstIndx);
                selectionMade = true;
            }
        }

        if (!selectionMade && prevSelSize > folderView_->selectionModel()->selectedIndexes().size()) {
            onSelChanged();
        }

        QModelIndex curIndex = folderView_->selectionModel()->currentIndex();
        if (curIndex.isValid()) {
            folderView_->childView()->scrollTo(curIndex, QAbstractItemView::EnsureVisible);
        }
        else {
            QModelIndexList selIndexes = folderView_->selectionModel()->selectedIndexes();
            curIndex = selIndexes.isEmpty() ? firstIndx : selIndexes.last();
            if (curIndex.isValid()) {
                folderView_->selectionModel()->setCurrentIndex(curIndex, QItemSelectionModel::NoUpdate);
                folderView_->childView()->scrollTo(curIndex, QAbstractItemView::EnsureVisible);
            }
        }
    }

    statusText_[StatusTextNormal] = formatStatusText();
    Q_EMIT statusChanged(StatusTextNormal, statusText_[StatusTextNormal]);
}

void TabPage::setCustomizedView(bool value, bool recursive) {
    if (folderSettings_.isCustomized() == value && folderSettings_.recursive() == recursive) {
        return;
    }

    Settings& settings = appSettings();
    if (value) {
        folderSettings_.setCustomized(value);
        folderSettings_.setRecursive(recursive);
        settings.saveFolderSettings(path(), folderSettings_);
    }
    else {
        settings.clearFolderSettings(path());
        folderSettings_ = settings.loadFolderSettings(path());

        // Restore settings from inheritance
        setShowHidden(folderSettings_.showHidden());
        setSortCaseSensitive(folderSettings_.sortCaseSensitive());
        setSortFolderFirst(folderSettings_.sortFolderFirst());
        sort(folderSettings_.sortColumn(), folderSettings_.sortOrder());
        setViewMode(folderSettings_.viewMode());
    }
}

void TabPage::goToCustomizedViewSource() {
    if (const auto inheritedPath = folderSettings_.inheritedPath()) {
        chdir(inheritedPath);
    }
}

}  // namespace Oneg4FM
