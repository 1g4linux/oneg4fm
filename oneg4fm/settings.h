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

#ifndef PCMANFM_SETTINGS_H
#define PCMANFM_SETTINGS_H

#include "panel/panel.h"

#include <QObject>
#include <QHash>
#include <QVariant>

namespace Oneg4FM {

enum OpenDirTargetType { OpenInCurrentTab, OpenInNewTab, OpenInNewWindow, OpenInLastActiveWindow };

class FolderSettings {
   public:
    FolderSettings()
        : isCustomized_(false),
          // NOTE: The default values of the following variables should be
          // the same as those of their corresponding variables in Settings:
          sortOrder_(Qt::AscendingOrder),
          sortColumn_(Panel::FolderModel::ColumnFileName),
          viewMode_(Panel::FolderView::DetailedListMode),
          showHidden_(false),
          sortFolderFirst_(true),
          sortCaseSensitive_(true),
          recursive_(false) {}

    bool isCustomized() const { return isCustomized_; }

    void setCustomized(bool value) { isCustomized_ = value; }

    Qt::SortOrder sortOrder() const { return sortOrder_; }

    void setSortOrder(Qt::SortOrder value) { sortOrder_ = value; }

    Panel::FolderModel::ColumnId sortColumn() const { return sortColumn_; }

    void setSortColumn(Panel::FolderModel::ColumnId value) { sortColumn_ = value; }

    Panel::FolderView::ViewMode viewMode() const { return viewMode_; }

    void setViewMode(Panel::FolderView::ViewMode value) { viewMode_ = value; }

    // Conversion functions (implemented in settings.cpp)
    static Panel::FolderView::ViewMode viewModeFromString(const QString& str);
    static Qt::SortOrder sortOrderFromString(const QString& str);
    static Panel::FolderModel::ColumnId sortColumnFromString(const QString& str);
    static Panel::SidePane::Mode sidePaneModeFromString(const QString& str);
    static OpenDirTargetType bookmarkOpenMethodFromString(const QString& str);

    bool sortFolderFirst() const { return sortFolderFirst_; }

    void setSortFolderFirst(bool value) { sortFolderFirst_ = value; }

    bool showHidden() const { return showHidden_; }

    void setShowHidden(bool value) { showHidden_ = value; }

    bool sortCaseSensitive() const { return sortCaseSensitive_; }

    void setSortCaseSensitive(bool value) { sortCaseSensitive_ = value; }

    bool recursive() const { return recursive_; }

    void setRecursive(bool value) { recursive_ = value; }

    Panel::FilePath inheritedPath() const { return inheritedPath_; }

    void seInheritedPath(const Panel::FilePath& path) { inheritedPath_ = std::move(path); }

   private:
    bool isCustomized_;
    Qt::SortOrder sortOrder_;
    Panel::FolderModel::ColumnId sortColumn_;
    Panel::FolderView::ViewMode viewMode_;
    bool showHidden_;
    bool sortFolderFirst_;
    bool sortCaseSensitive_;
    bool recursive_;
    Panel::FilePath inheritedPath_;
    // columns?
};

class Settings : public QObject {
    Q_OBJECT
   public:
    enum IconType { Small, Big, Thumbnail };

    Settings();
    virtual ~Settings();

    bool load(QString profile = QStringLiteral("default"));
    bool save(QString profile = QString());

    static QString xdgUserConfigDir();
    static const QList<int>& iconSizes(IconType type);
    static int wallpaperModeFromString(const QString str);

    // String conversion utilities
    static const char* viewModeToString(Panel::FolderView::ViewMode value);
    static const char* sortOrderToString(Qt::SortOrder order);
    static const char* sortColumnToString(Panel::FolderModel::ColumnId value);
    static const char* sidePaneModeToString(Panel::SidePane::Mode value);

    QString profileDir(QString profile, bool useFallback = false);

    // setter/getter functions
    QString profileName() const { return profileName_; }

    bool supportTrash() const { return supportTrash_; }

    QString fallbackIconThemeName() const { return fallbackIconThemeName_; }

    bool useFallbackIconTheme() const { return useFallbackIconTheme_; }

    void setFallbackIconThemeName(QString iconThemeName) { fallbackIconThemeName_ = iconThemeName; }

    bool singleWindowMode() const { return singleWindowMode_; }

    void setSingleWindowMode(bool singleWindowMode) { singleWindowMode_ = singleWindowMode; }

    OpenDirTargetType bookmarkOpenMethod() { return bookmarkOpenMethod_; }

    void setBookmarkOpenMethod(OpenDirTargetType bookmarkOpenMethod) { bookmarkOpenMethod_ = bookmarkOpenMethod; }

    bool preservePermissions() const { return preservePermissions_; }
    void setPreservePermissions(bool preserve) { preservePermissions_ = preserve; }

    QString terminal() { return terminal_; }
    void setTerminal(QString terminalCommand);

    QString archiver() const { return archiver_; }

    void setArchiver(QString archiver) {
        archiver_ = archiver;
        Panel::Archiver::setDefaultArchiverByName(archiver_.toLocal8Bit().constData());
    }

    bool alwaysShowTabs() const { return alwaysShowTabs_; }

    void setAlwaysShowTabs(bool alwaysShowTabs) { alwaysShowTabs_ = alwaysShowTabs; }

    bool showTabClose() const { return showTabClose_; }

    void setShowTabClose(bool showTabClose) { showTabClose_ = showTabClose; }

    bool switchToNewTab() const { return switchToNewTab_; }

    void setSwitchToNewTab(bool showTabClose) { switchToNewTab_ = showTabClose; }

    bool reopenLastTabs() const { return reopenLastTabs_; }

    void setReopenLastTabs(bool reopenLastTabs) { reopenLastTabs_ = reopenLastTabs; }

    int splitViewTabsNum() const { return splitViewTabsNum_; }

    void setSplitViewTabsNum(int n) { splitViewTabsNum_ = n; }

    QStringList tabPaths() const { return tabPaths_; }

    void setTabPaths(const QStringList& tabPaths) { tabPaths_ = tabPaths; }

    bool rememberWindowSize() const { return rememberWindowSize_; }

    void setRememberWindowSize(bool rememberWindowSize) { rememberWindowSize_ = rememberWindowSize; }

    int windowWidth() const {
        if (rememberWindowSize_) {
            return lastWindowWidth_;
        }
        else {
            return fixedWindowWidth_;
        }
    }

    int windowHeight() const {
        if (rememberWindowSize_) {
            return lastWindowHeight_;
        }
        else {
            return fixedWindowHeight_;
        }
    }

    int windowX() const { return lastWindowX_; }
    int windowY() const { return lastWindowY_; }

    bool windowMaximized() const {
        if (rememberWindowSize_) {
            return lastWindowMaximized_;
        }
        else {
            return false;
        }
    }

    int fixedWindowWidth() const { return fixedWindowWidth_; }

    void setFixedWindowWidth(int fixedWindowWidth) { fixedWindowWidth_ = fixedWindowWidth; }

    int fixedWindowHeight() const { return fixedWindowHeight_; }

    void setFixedWindowHeight(int fixedWindowHeight) { fixedWindowHeight_ = fixedWindowHeight; }

    void setLastWindowWidth(int lastWindowWidth) { lastWindowWidth_ = lastWindowWidth; }

    void setLastWindowHeight(int lastWindowHeight) { lastWindowHeight_ = lastWindowHeight; }

    void setLastWindowX(int x) { lastWindowX_ = x; }
    void setLastWindowY(int y) { lastWindowY_ = y; }

    void setLastWindowMaximized(bool lastWindowMaximized) { lastWindowMaximized_ = lastWindowMaximized; }

    int splitterPos() const { return splitterPos_; }

    void setSplitterPos(int splitterPos) { splitterPos_ = splitterPos; }

    bool isSidePaneVisible() const { return sidePaneVisible_; }

    void showSidePane(bool show) { sidePaneVisible_ = show; }

    Panel::SidePane::Mode sidePaneMode() const { return sidePaneMode_; }

    void setSidePaneMode(Panel::SidePane::Mode sidePaneMode) { sidePaneMode_ = sidePaneMode; }

    bool showMenuBar() const { return showMenuBar_; }

    void setShowMenuBar(bool showMenuBar) { showMenuBar_ = showMenuBar; }

    bool splitView() const { return splitView_; }

    void setSplitView(bool split) { splitView_ = split; }

    Panel::FolderView::ViewMode viewMode() const { return viewMode_; }

    void setViewMode(Panel::FolderView::ViewMode viewMode) { viewMode_ = viewMode; }

    bool showHidden() const { return showHidden_; }

    void setShowHidden(bool showHidden) { showHidden_ = showHidden; }

    bool sortCaseSensitive() const { return sortCaseSensitive_; }

    void setSortCaseSensitive(bool value) { sortCaseSensitive_ = value; }

    QSet<QString> getHiddenPlaces() const { return hiddenPlaces_; }

    void setHiddenPlace(const QString& str, bool hide) {
        if (hide) {
            hiddenPlaces_ << str;
        }
        else {
            hiddenPlaces_.remove(str);
        }
    }

    Qt::SortOrder sortOrder() const { return sortOrder_; }

    void setSortOrder(Qt::SortOrder sortOrder) { sortOrder_ = sortOrder; }

    Panel::FolderModel::ColumnId sortColumn() const { return sortColumn_; }

    void setSortColumn(Panel::FolderModel::ColumnId sortColumn) { sortColumn_ = sortColumn; }

    bool sortFolderFirst() const { return sortFolderFirst_; }

    void setSortFolderFirst(bool folderFirst) { sortFolderFirst_ = folderFirst; }

    bool showFilter() const { return showFilter_; }

    void setShowFilter(bool value) { showFilter_ = value; }

    bool pathBarButtons() const { return pathBarButtons_; }

    void setPathBarButtons(bool value) { pathBarButtons_ = value; }

    // settings for use with libfm
    bool singleClick() const { return singleClick_; }

    void setSingleClick(bool singleClick) { singleClick_ = singleClick; }

    int autoSelectionDelay() const { return autoSelectionDelay_; }

    void setAutoSelectionDelay(int value) { autoSelectionDelay_ = value; }

    bool ctrlRightClick() const { return ctrlRightClick_; }

    void setCtrlRightClick(bool value) { ctrlRightClick_ = value; }

    bool useTrash() const {
        if (!supportTrash_) {
            return false;
        }
        return useTrash_;
    }

    void setUseTrash(bool useTrash) { useTrash_ = useTrash; }

    bool confirmDelete() const { return confirmDelete_; }

    void setConfirmDelete(bool confirmDelete) { confirmDelete_ = confirmDelete; }

    bool noUsbTrash() const { return noUsbTrash_; }

    void setNoUsbTrash(bool noUsbTrash) {
        noUsbTrash_ = noUsbTrash;
        fm_config->no_usb_trash =
            noUsbTrash_;  // also set this to libfm since FmFileOpsJob reads this config value before trashing files.
    }

    bool confirmTrash() const { return confirmTrash_; }

    void setConfirmTrash(bool value) { confirmTrash_ = value; }

    bool quickExec() const { return quickExec_; }

    void setQuickExec(bool value) {
        quickExec_ = value;
        fm_config->quick_exec = quickExec_;
    }

    bool selectNewFiles() const { return selectNewFiles_; }

    void setSelectNewFiles(bool value) { selectNewFiles_ = value; }

    int bigIconSize() const { return bigIconSize_; }

    void setBigIconSize(int bigIconSize) { bigIconSize_ = bigIconSize; }

    int smallIconSize() const { return smallIconSize_; }

    void setSmallIconSize(int smallIconSize) { smallIconSize_ = smallIconSize; }

    int sidePaneIconSize() const { return sidePaneIconSize_; }

    void setSidePaneIconSize(int sidePaneIconSize) { sidePaneIconSize_ = sidePaneIconSize; }

    int thumbnailIconSize() const { return thumbnailIconSize_; }

    QSize folderViewCellMargins() const { return folderViewCellMargins_; }

    void setFolderViewCellMargins(QSize size) { folderViewCellMargins_ = size; }

    bool openWithDefaultFileManager() const { return openWithDefaultFileManager_; }

    void setOpenWithDefaultFileManager(bool open) { openWithDefaultFileManager_ = open; }

    bool allSticky() const { return allSticky_; }

    void setAllSticky(bool sticky) { allSticky_ = sticky; }

    bool showThumbnails() { return showThumbnails_; }

    void setShowThumbnails(bool show) { showThumbnails_ = show; }

    void setThumbnailLocalFilesOnly(bool value) { Panel::ThumbnailJob::setLocalFilesOnly(value); }

    bool thumbnailLocalFilesOnly() const { return Panel::ThumbnailJob::localFilesOnly(); }

    int maxThumbnailFileSize() const { return Panel::ThumbnailJob::maxThumbnailFileSize(); }

    void setMaxThumbnailFileSize(int size) { Panel::ThumbnailJob::setMaxThumbnailFileSize(size); }

    int maxExternalThumbnailFileSize() const { return Panel::ThumbnailJob::maxExternalThumbnailFileSize(); }

    void setMaxExternalThumbnailFileSize(int size) { Panel::ThumbnailJob::setMaxExternalThumbnailFileSize(size); }

    void setThumbnailIconSize(int thumbnailIconSize) { thumbnailIconSize_ = thumbnailIconSize; }

    bool siUnit() { return siUnit_; }

    void setSiUnit(bool siUnit) {
        siUnit_ = siUnit;
        // override libfm FmConfig settings. FIXME: should we do this?
        fm_config->si_unit = (gboolean)siUnit_;
    }

    bool backupAsHidden() const { return backupAsHidden_; }

    void setBackupAsHidden(bool value) {
        backupAsHidden_ = value;
        fm_config->backup_as_hidden =
            backupAsHidden_;  // also set this to libfm since fm_file_info_is_hidden() reads this value internally.
    }

    bool showFullNames() const { return showFullNames_; }

    void setShowFullNames(bool value) { showFullNames_ = value; }

    bool shadowHidden() const { return shadowHidden_; }

    void setShadowHidden(bool value) { shadowHidden_ = value; }

    bool noItemTooltip() const { return noItemTooltip_; }

    void setNoItemTooltip(bool noTooltip) { noItemTooltip_ = noTooltip; }

    bool scrollPerPixel() const { return scrollPerPixel_; }

    void setScrollPerPixel(bool perPixel) { scrollPerPixel_ = perPixel; }

    bool onlyUserTemplates() const { return onlyUserTemplates_; }

    void setOnlyUserTemplates(bool value) {
        onlyUserTemplates_ = value;
        fm_config->only_user_templates = onlyUserTemplates_;
    }

    bool templateTypeOnce() const { return templateTypeOnce_; }

    void setTemplateTypeOnce(bool value) {
        templateTypeOnce_ = value;
        fm_config->template_type_once = templateTypeOnce_;
    }

    bool templateRunApp() const { return templateRunApp_; }

    void setTemplateRunApp(bool value) {
        templateRunApp_ = value;
        fm_config->template_run_app = templateRunApp_;
    }

    // per-folder settings
    FolderSettings loadFolderSettings(const Panel::FilePath& path) const;

    void saveFolderSettings(const Panel::FilePath& path, const FolderSettings& settings);

    void clearFolderSettings(const Panel::FilePath& path) const;

    bool searchNameCaseInsensitive() const { return searchNameCaseInsensitive_; }

    void setSearchNameCaseInsensitive(bool caseInsensitive) { searchNameCaseInsensitive_ = caseInsensitive; }

    bool searchContentCaseInsensitive() const { return searchContentCaseInsensitive_; }

    void setsearchContentCaseInsensitive(bool caseInsensitive) { searchContentCaseInsensitive_ = caseInsensitive; }

    bool searchNameRegexp() const { return searchNameRegexp_; }

    void setSearchNameRegexp(bool reg) { searchNameRegexp_ = reg; }

    bool searchContentRegexp() const { return searchContentRegexp_; }

    void setSearchContentRegexp(bool reg) { searchContentRegexp_ = reg; }

    bool searchRecursive() const { return searchRecursive_; }

    void setSearchRecursive(bool rec) { searchRecursive_ = rec; }

    bool searchhHidden() const { return searchhHidden_; }

    void setSearchhHidden(bool hidden) { searchhHidden_ = hidden; }

    int maxSearchHistory() const { return maxSearchHistory_; }

    void setMaxSearchHistory(int max);

    void clearSearchHistory();

    QStringList namePatterns() const { return namePatterns_; }

    void addNamePattern(const QString& pattern);

    QStringList contentPatterns() const { return contentPatterns_; }

    void addContentPattern(const QString& pattern);

    QList<int> getCustomColumnWidths() const {
        QList<int> l;
        for (auto width : std::as_const(customColumnWidths_)) {
            l << width.toInt();
        }
        return l;
    }

    void setCustomColumnWidths(const QList<int>& widths) {
        customColumnWidths_.clear();
        for (auto width : widths) {
            customColumnWidths_ << QVariant(width);
        }
    }

    QList<int> getHiddenColumns() const {
        QList<int> l;
        for (auto width : std::as_const(hiddenColumns_)) {
            l << width.toInt();
        }
        return l;
    }

    void setHiddenColumns(const QList<int>& columns) {
        hiddenColumns_.clear();
        for (auto column : columns) {
            hiddenColumns_ << QVariant(column);
        }
    }

   private:
    bool loadFile(QString filePath);
    bool saveFile(QString filePath);

    int toIconSize(int size, IconType type) const;

   private:
    QString profileName_;
    bool supportTrash_;

    // Oneg4FM specific
    QString fallbackIconThemeName_;
    bool useFallbackIconTheme_;

    bool singleWindowMode_;
    OpenDirTargetType bookmarkOpenMethod_;
    bool preservePermissions_;
    QString terminal_;

    bool alwaysShowTabs_;
    bool showTabClose_;
    bool switchToNewTab_;
    bool reopenLastTabs_;
    int splitViewTabsNum_;  // number of tabs in the first view frame when reopening last tabs
    QStringList tabPaths_;
    bool rememberWindowSize_;
    int fixedWindowWidth_;
    int fixedWindowHeight_;
    int lastWindowWidth_;
    int lastWindowHeight_;
    int lastWindowX_;
    int lastWindowY_;
    bool lastWindowMaximized_;
    int splitterPos_;
    bool sidePaneVisible_;
    Panel::SidePane::Mode sidePaneMode_;
    bool showMenuBar_;
    bool splitView_;

    Panel::FolderView::ViewMode viewMode_;
    bool showHidden_;
    Qt::SortOrder sortOrder_;
    Panel::FolderModel::ColumnId sortColumn_;
    bool sortFolderFirst_;
    bool sortCaseSensitive_;
    bool showFilter_;
    bool pathBarButtons_;

    // settings for use with libfm
    bool singleClick_;
    int autoSelectionDelay_;
    bool ctrlRightClick_;
    bool useTrash_;
    bool confirmDelete_;
    bool noUsbTrash_;    // do not trash files on usb removable devices
    bool confirmTrash_;  // Confirm before moving files into "trash can"
    bool quickExec_;     // Don't ask options on launch executable file
    bool selectNewFiles_;

    bool showThumbnails_;

    QString archiver_;
    bool siUnit_;
    bool backupAsHidden_;
    bool showFullNames_;
    bool shadowHidden_;
    bool noItemTooltip_;
    bool scrollPerPixel_;

    QSet<QString> hiddenPlaces_;

    int bigIconSize_;
    int smallIconSize_;
    int sidePaneIconSize_;
    int thumbnailIconSize_;

    bool onlyUserTemplates_;
    bool templateTypeOnce_;
    bool templateRunApp_;

    QSize folderViewCellMargins_;

    bool openWithDefaultFileManager_;

    bool allSticky_;

    // search settings
    bool searchNameCaseInsensitive_;
    bool searchContentCaseInsensitive_;
    bool searchNameRegexp_;
    bool searchContentRegexp_;
    bool searchRecursive_;
    bool searchhHidden_;
    int maxSearchHistory_;
    QStringList namePatterns_;
    QStringList contentPatterns_;

    // detailed list columns
    QList<QVariant> customColumnWidths_;
    QList<QVariant> hiddenColumns_;

    // Unknown profile keys are preserved for the loaded profile on save.
    QString loadedProfileSettingsPath_;
    QHash<QString, QVariant> preservedUnknownProfileKeys_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_SETTINGS_H
