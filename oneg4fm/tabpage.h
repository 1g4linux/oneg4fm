/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef FM_TABPAGE_H
#define FM_TABPAGE_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include "panel/panel.h"
#include "view.h"
#include "settings.h"
#include "imagemagick_qt.h"

class QScrollBar;

namespace Oneg4FM {

class Launcher;

class ProxyFilter : public Panel::ProxyFolderModelFilter {
   public:
    ProxyFilter() : fullName_{true} {}
    bool filterAcceptsRow(const Panel::ProxyFolderModel* model,
                          const std::shared_ptr<const Panel::FileInfo>& info) const;
    virtual ~ProxyFilter() {}
    QString getFilterStr() { return filterStr_; }
    void setFilterStr(QString str) { filterStr_ = str; }
    void filterFullName(bool fullName) { fullName_ = fullName; }

   private:
    bool fullName_;
    QString filterStr_;
};

//==================================================

class FilterEdit : public QLineEdit {
    Q_OBJECT
   public:
    FilterEdit(QWidget* parent = nullptr);
    ~FilterEdit() {};
    void keyPressed(QKeyEvent* event);

   protected:
    virtual void focusOutEvent(QFocusEvent* event) override {
        Q_EMIT lostFocus();
        QLineEdit::focusOutEvent(event);
    }
    virtual void keyPressEvent(QKeyEvent* event) override;

   Q_SIGNALS:
    void lostFocus();
};

class FilterBar : public QWidget {
    Q_OBJECT
   public:
    FilterBar(QWidget* parent = nullptr);
    ~FilterBar() {};

    void focusBar() { filterEdit_->setFocus(); }
    void clear() { filterEdit_->clear(); }
    void keyPressed(QKeyEvent* event) { filterEdit_->keyPressed(event); }

   Q_SIGNALS:
    void textChanged(const QString& text);
    void lostFocus();

   private:
    FilterEdit* filterEdit_;
};

//==================================================

class TabPage : public QWidget {
    Q_OBJECT

   public:
    enum StatusTextType { StatusTextNormal, StatusTextSelectedFiles, StatusTextFSInfo, StatusTextNum };

   public:
    explicit TabPage(QWidget* parent = nullptr);
    virtual ~TabPage();

    void chdir(Panel::FilePath newPath, bool addHistory = true);

    Panel::FolderView::ViewMode viewMode() { return folderSettings_.viewMode(); }

    void setViewMode(Panel::FolderView::ViewMode mode);

    void sort(int col, Qt::SortOrder order = Qt::AscendingOrder);

    int sortColumn() { return folderSettings_.sortColumn(); }

    Qt::SortOrder sortOrder() { return folderSettings_.sortOrder(); }

    bool sortFolderFirst() { return folderSettings_.sortFolderFirst(); }
    void setSortFolderFirst(bool value);

    bool sortCaseSensitive() { return folderSettings_.sortCaseSensitive(); }

    void setSortCaseSensitive(bool value);

    bool showHidden() { return proxyModel_->showHidden(); }

    void setShowHidden(bool showHidden);

    void setShowThumbnails(bool showThumbnails);

    void saveFolderSorting();

    Panel::FilePath path() { return folder_ ? folder_->path() : Panel::FilePath(); }

    QString pathName();

    const std::shared_ptr<Panel::Folder>& folder() { return folder_; }

    Panel::FolderModel* folderModel() { return reinterpret_cast<Panel::FolderModel*>(folderModel_); }

    View* folderView() { return folderView_; }

    Panel::BrowseHistory& browseHistory() { return history_; }

    Panel::FileInfoList selectedFiles() { return folderView_->selectedFiles(); }

    Panel::FilePathList selectedFilePaths() { return folderView_->selectedFilePaths(); }

    void selectAll();

    void deselectAll();

    void invertSelection();

    void reload();

    QString title() const { return title_; }

    QString statusText(StatusTextType type = StatusTextNormal) const { return statusText_[type]; }

    bool canBackward() { return history_.canBackward(); }

    void backward();

    bool canForward() { return history_.canForward(); }

    void forward();

    void jumpToHistory(int index);

    bool canUp();

    void up();

    void updateFromSettings(Settings& settings);

    void setFileLauncher(Panel::FileLauncher* launcher) { folderView_->setFileLauncher(launcher); }

    Panel::FileLauncher* fileLauncher() { return folderView_->fileLauncher(); }

    QString getFilterStr() {
        if (proxyFilter_) {
            return proxyFilter_->getFilterStr();
        }
        return QString();
    }

    void setFilterStr(QString str) {
        if (proxyFilter_) {
            proxyFilter_->setFilterStr(str);
        }
    }

    void applyFilter();

    bool hasCustomizedView() const { return folderSettings_.isCustomized(); }
    bool hasRecursiveCustomizedView() const { return folderSettings_.isCustomized() && folderSettings_.recursive(); }
    bool hasInheritedCustomizedView() const {
        return !folderSettings_.isCustomized() && folderSettings_.inheritedPath().isValid();
    }

    void setCustomizedView(bool value, bool recursive = false);

    void goToCustomizedViewSource();

    void transientFilterBar(bool transient);

    void showFilterBar();
    bool isFilterBarVisible() const { return (filterBar_ && filterBar_->isVisible()); }
    void clearFilter() {
        if (filterBar_) {
            filterBar_->clear();
        }
    }

    void createShortcut();

    void setFilesToSelect(const Panel::FilePathList& files) { filesToSelect_ = files; }

   Q_SIGNALS:
    void statusChanged(int type, QString statusText);
    void titleChanged();
    void sortFilterChanged();
    void forwardRequested();
    void backwardRequested();
    void folderUnmounted();
    void backspacePressed();

   protected:
    virtual bool eventFilter(QObject* watched, QEvent* event);

   protected Q_SLOTS:
    void onSelChanged();
    void onUiUpdated();
    void onFileSizeChanged(const QModelIndex& index);
    void onFilesAdded(const Panel::FileInfoList files);
    void onFilterStringChanged(QString str);
    void onLosingFilterBarFocus();

   private:
    void freeFolder();
    QScrollBar* historyScrollBar() const;
    int currentHistoryScrollPos() const;
    void saveCurrentHistoryScrollPos();
    void restoreCurrentHistoryScrollPos();
    QString formatStatusText();
    void localizeTitle(const Panel::FilePath& path);

    void onFolderStartLoading();
    void onFolderFinishLoading();

    // FIXME: this API design is bad and might be removed later
    void onFolderError(const Panel::GErrorPtr& err,
                       Panel::Job::ErrorSeverity severity,
                       Panel::Job::ErrorAction& response);

    void onFolderFsInfo();
    void onFolderRemoved();
    void onFolderUnmount();
    void onFolderContentChanged();

   private:
    View* folderView_;
    Panel::CachedFolderModel* folderModel_;
    ImageMagickProxyFolderModel* proxyModel_;
    ProxyFilter* proxyFilter_;
    QVBoxLayout* verticalLayout;
    std::shared_ptr<Panel::Folder> folder_;
    QString title_;
    QString statusText_[StatusTextNum];
    Panel::BrowseHistory history_;    // browsing history
    Panel::FilePath lastFolderPath_;  // last browsed folder
    bool overrideCursor_;
    FolderSettings folderSettings_;
    QTimer* selectionTimer_;
    FilterBar* filterBar_;
    QStringList filesToTrust_;
    Panel::FilePathList filesToSelect_;  // files to select
    bool changingDir_;                   // chdir is in progress
};

}  // namespace Oneg4FM

#endif  // FM_TABPAGE_H
