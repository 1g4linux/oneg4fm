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

#include "settings.h"

#include "panel/panel.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include "../src/ui/fsqt.h"

namespace Oneg4FM {

inline static const char* bookmarkOpenMethodToString(OpenDirTargetType value);

Settings::Settings()
    : QObject(),
      supportTrash_(false),  // trash disabled in this build
      fallbackIconThemeName_(),
      useFallbackIconTheme_(QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")),
      singleWindowMode_(false),
      bookmarkOpenMethod_(OpenInCurrentTab),
      preservePermissions_(false),
      terminal_(),
      alwaysShowTabs_(true),
      showTabClose_(true),
      switchToNewTab_(false),
      reopenLastTabs_(false),
      splitViewTabsNum_(0),
      rememberWindowSize_(true),
      fixedWindowWidth_(640),
      fixedWindowHeight_(480),
      lastWindowWidth_(640),
      lastWindowHeight_(480),
      lastWindowX_(-1),
      lastWindowY_(-1),
      lastWindowMaximized_(false),
      splitterPos_(120),
      sidePaneVisible_(true),
      sidePaneMode_(Panel::SidePane::ModePlaces),
      showMenuBar_(true),
      splitView_(false),
      viewMode_(Panel::FolderView::DetailedListMode),
      showHidden_(false),
      sortOrder_(Qt::AscendingOrder),
      sortColumn_(Panel::FolderModel::ColumnFileName),
      sortFolderFirst_(true),
      sortCaseSensitive_(false),
      showFilter_(false),
      pathBarButtons_(true),
      // settings for use with libfm
      singleClick_(false),
      autoSelectionDelay_(600),
      ctrlRightClick_(false),
      useTrash_(true),
      confirmDelete_(true),
      noUsbTrash_(false),
      confirmTrash_(false),
      quickExec_(false),
      selectNewFiles_(false),
      showThumbnails_(true),
      archiver_(),
      siUnit_(false),
      backupAsHidden_(false),
      showFullNames_(true),
      shadowHidden_(true),
      noItemTooltip_(false),
      scrollPerPixel_(true),
      bigIconSize_(48),
      smallIconSize_(24),
      sidePaneIconSize_(24),
      thumbnailIconSize_(128),
      onlyUserTemplates_(false),
      templateTypeOnce_(false),
      templateRunApp_(false),
      folderViewCellMargins_(QSize(3, 3)),
      openWithDefaultFileManager_(false),
      allSticky_(false),
      searchNameCaseInsensitive_(false),
      searchContentCaseInsensitive_(false),
      searchNameRegexp_(true),
      searchContentRegexp_(true),
      searchRecursive_(false),
      searchhHidden_(false),
      maxSearchHistory_(0) {}

Settings::~Settings() = default;

QString Settings::xdgUserConfigDir() {
    QString dirName;
    // WARNING: Don't use XDG_CONFIG_HOME with root because it might
    // give the user config directory if gksu-properties is set to su.
    if (geteuid() != 0) {  // non-root user
        dirName = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    if (dirName.isEmpty()) {
        dirName = QDir::homePath() + QLatin1String("/.config");
    }
    return dirName;
}

QString Settings::profileDir(QString profile, bool useFallback) {
    // try user-specific config file first
    QString dirName =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/oneg4fm/") + profile;
    QDir dir(dirName);

    // if user config dir does not exist, try system-wide config dirs instead
    if (!dir.exists() && useFallback) {
        QString fallbackDir;
        const QStringList confList = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation);
        for (const auto& thisConf : confList) {
            fallbackDir = thisConf + QStringLiteral("/oneg4fm/") + profile;
            if (fallbackDir == dirName) {
                continue;
            }
            dir.setPath(fallbackDir);
            if (dir.exists()) {
                dirName = fallbackDir;
                break;
            }
        }
    }
    return dirName;
}

bool Settings::load(QString profile) {
    profileName_ = profile;
    QString fileName = profileDir(profile, true) + QStringLiteral("/settings.conf");
    bool ret = loadFile(fileName);
    return ret;
}

bool Settings::save(QString profile) {
    QString fileName = profileDir(profile.isEmpty() ? profileName_ : profile) + QStringLiteral("/settings.conf");
    bool ret = saveFile(fileName);
    return ret;
}

bool Settings::loadFile(QString filePath) {
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("System"));
    fallbackIconThemeName_ = settings.value(QStringLiteral("FallbackIconThemeName")).toString();
    if (fallbackIconThemeName_.isEmpty()) {
        // Use Papirus-Dark as fallback icon theme if available
        fallbackIconThemeName_ = QLatin1String("Papirus-Dark");

        // Check if Papirus-Dark is actually installed
        bool papirusInstalled = false;
        const QStringList paths = QIcon::themeSearchPaths();
        for (const QString& path : paths) {
            if (QDir(path).exists(fallbackIconThemeName_)) {
                papirusInstalled = true;
                break;
            }
        }

        if (!papirusInstalled) {
            // Try to find any other installed theme
            for (const QString& path : paths) {
                QDir dir(path);
                const QStringList themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& theme : themes) {
                    if (theme != QLatin1String("hicolor") && theme != QLatin1String("locolor")) {
                        fallbackIconThemeName_ = theme;
                        papirusInstalled = true;  // Found something
                        break;
                    }
                }
                if (papirusInstalled) {
                    break;
                }
            }
        }
    }
    setTerminal(settings.value(QStringLiteral("Terminal"), QStringLiteral("xterm")).toString());
    setArchiver(settings.value(QStringLiteral("Archiver"), QStringLiteral("file-roller")).toString());
    setSiUnit(settings.value(QStringLiteral("SIUnit"), false).toBool());

    setOnlyUserTemplates(settings.value(QStringLiteral("OnlyUserTemplates"), false).toBool());
    setTemplateTypeOnce(settings.value(QStringLiteral("TemplateTypeOnce"), false).toBool());
    setTemplateRunApp(settings.value(QStringLiteral("TemplateRunApp"), false).toBool());

    settings.endGroup();

    settings.beginGroup(QStringLiteral("Behavior"));
    singleWindowMode_ = settings.value(QStringLiteral("SingleWindowMode"), false).toBool();
    bookmarkOpenMethod_ =
        FolderSettings::bookmarkOpenMethodFromString(settings.value(QStringLiteral("BookmarkOpenMethod")).toString());
    preservePermissions_ = settings.value(QStringLiteral("PreservePermissions"), false).toBool();
    // settings for use with libfm
    useTrash_ = false;  // trash disabled
    singleClick_ = settings.value(QStringLiteral("SingleClick"), false).toBool();
    autoSelectionDelay_ = settings.value(QStringLiteral("AutoSelectionDelay"), 600).toInt();
    ctrlRightClick_ = settings.value(QStringLiteral("CtrlRightClick"), false).toBool();
    confirmDelete_ = settings.value(QStringLiteral("ConfirmDelete"), true).toBool();
    setNoUsbTrash(settings.value(QStringLiteral("NoUsbTrash"), false).toBool());
    confirmTrash_ = settings.value(QStringLiteral("ConfirmTrash"), false).toBool();
    setQuickExec(settings.value(QStringLiteral("QuickExec"), false).toBool());
    selectNewFiles_ = settings.value(QStringLiteral("SelectNewFiles"), false).toBool();
    openWithDefaultFileManager_ = settings.value(QStringLiteral("OpenWithDefaultFileManager"), false).toBool();
    allSticky_ = settings.value(QStringLiteral("AllSticky"), false).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Thumbnail"));
    showThumbnails_ = settings.value(QStringLiteral("ShowThumbnails"), true).toBool();
    setMaxThumbnailFileSize(settings.value(QStringLiteral("MaxThumbnailFileSize"), 4096).toInt());
    setMaxExternalThumbnailFileSize(settings.value(QStringLiteral("MaxExternalThumbnailFileSize"), -1).toInt());
    setThumbnailLocalFilesOnly(settings.value(QStringLiteral("ThumbnailLocalFilesOnly"), true).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("FolderView"));
    viewMode_ = FolderSettings::viewModeFromString(
        settings.value(QStringLiteral("Mode"), QStringLiteral("detailed")).toString());
    showHidden_ = settings.value(QStringLiteral("ShowHidden"), false).toBool();
    sortOrder_ = FolderSettings::sortOrderFromString(settings.value(QStringLiteral("SortOrder")).toString());
    sortColumn_ = FolderSettings::sortColumnFromString(settings.value(QStringLiteral("SortColumn")).toString());
    sortFolderFirst_ = settings.value(QStringLiteral("SortFolderFirst"), true).toBool();
    sortCaseSensitive_ = settings.value(QStringLiteral("SortCaseSensitive"), false).toBool();
    showFilter_ = settings.value(QStringLiteral("ShowFilter"), false).toBool();

    setBackupAsHidden(settings.value(QStringLiteral("BackupAsHidden"), false).toBool());
    showFullNames_ = settings.value(QStringLiteral("ShowFullNames"), true).toBool();
    shadowHidden_ = settings.value(QStringLiteral("ShadowHidden"), true).toBool();
    noItemTooltip_ = settings.value(QStringLiteral("NoItemTooltip"), false).toBool();
    scrollPerPixel_ = settings.value(QStringLiteral("ScrollPerPixel"), true).toBool();

    // override config in libfm's FmConfig
    bigIconSize_ = toIconSize(settings.value(QStringLiteral("BigIconSize"), 48).toInt(), Big);
    smallIconSize_ = toIconSize(settings.value(QStringLiteral("SmallIconSize"), 24).toInt(), Small);
    sidePaneIconSize_ = toIconSize(settings.value(QStringLiteral("SidePaneIconSize"), 24).toInt(), Small);
    thumbnailIconSize_ = toIconSize(settings.value(QStringLiteral("ThumbnailIconSize"), 128).toInt(), Thumbnail);

    folderViewCellMargins_ =
        (settings.value(QStringLiteral("FolderViewCellMargins"), QSize(3, 3)).toSize().expandedTo(QSize(0, 0)))
            .boundedTo(QSize(48, 48));

    // detailed list columns
    customColumnWidths_ = settings.value(QStringLiteral("CustomColumnWidths")).toList();
    hiddenColumns_ = settings.value(QStringLiteral("HiddenColumns")).toList();

    settings.endGroup();

    settings.beginGroup(QStringLiteral("Places"));
    QStringList hiddenPlacesList = settings.value(QStringLiteral("HiddenPlaces")).toStringList();
    hiddenPlaces_ = QSet<QString>(hiddenPlacesList.begin(), hiddenPlacesList.end());
    // Force-hide unsupported/disabled virtual locations
    hiddenPlaces_ << QStringLiteral("computer:///") << QStringLiteral("network:///") << QStringLiteral("trash:///");
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Window"));
    fixedWindowWidth_ = settings.value(QStringLiteral("FixedWidth"), 640).toInt();
    fixedWindowHeight_ = settings.value(QStringLiteral("FixedHeight"), 480).toInt();
    lastWindowWidth_ = settings.value(QStringLiteral("LastWindowWidth"), 640).toInt();
    lastWindowHeight_ = settings.value(QStringLiteral("LastWindowHeight"), 480).toInt();
    lastWindowX_ = settings.value(QStringLiteral("LastWindowX"), -1).toInt();
    lastWindowY_ = settings.value(QStringLiteral("LastWindowY"), -1).toInt();
    lastWindowMaximized_ = settings.value(QStringLiteral("LastWindowMaximized"), false).toBool();
    rememberWindowSize_ = settings.value(QStringLiteral("RememberWindowSize"), true).toBool();
    alwaysShowTabs_ = settings.value(QStringLiteral("AlwaysShowTabs"), true).toBool();
    showTabClose_ = settings.value(QStringLiteral("ShowTabClose"), true).toBool();
    switchToNewTab_ = settings.value(QStringLiteral("SwitchToNewTab"), false).toBool();
    reopenLastTabs_ = settings.value(QStringLiteral("ReopenLastTabs"), false).toBool();
    tabPaths_ = settings.value(QStringLiteral("TabPaths")).toStringList();
    splitViewTabsNum_ = settings.value(QStringLiteral("SplitViewTabsNum")).toInt();
    splitterPos_ = settings.value(QStringLiteral("SplitterPos"), 150).toInt();
    sidePaneVisible_ = settings.value(QStringLiteral("SidePaneVisible"), true).toBool();
    sidePaneMode_ = FolderSettings::sidePaneModeFromString(settings.value(QStringLiteral("SidePaneMode")).toString());
    showMenuBar_ = settings.value(QStringLiteral("ShowMenuBar"), true).toBool();
    splitView_ = settings.value(QStringLiteral("SplitView"), false).toBool();
    pathBarButtons_ = settings.value(QStringLiteral("PathBarButtons"), true).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Search"));
    searchNameCaseInsensitive_ = settings.value(QStringLiteral("searchNameCaseInsensitive"), false).toBool();
    searchContentCaseInsensitive_ = settings.value(QStringLiteral("searchContentCaseInsensitive"), false).toBool();
    searchNameRegexp_ = settings.value(QStringLiteral("searchNameRegexp"), true).toBool();
    searchContentRegexp_ = settings.value(QStringLiteral("searchContentRegexp"), true).toBool();
    searchRecursive_ = settings.value(QStringLiteral("searchRecursive"), false).toBool();
    searchhHidden_ = settings.value(QStringLiteral("searchhHidden"), false).toBool();
    maxSearchHistory_ = std::clamp(settings.value(QStringLiteral("MaxSearchHistory"), 0).toInt(), 0, 50);
    namePatterns_ = settings.value(QStringLiteral("NamePatterns")).toStringList();
    namePatterns_.removeDuplicates();
    contentPatterns_ = settings.value(QStringLiteral("ContentPatterns")).toStringList();
    contentPatterns_.removeDuplicates();
    settings.endGroup();

    return true;
}

bool Settings::saveFile(QString filePath) {
    QSettings settings(filePath, QSettings::IniFormat);

    settings.beginGroup(QStringLiteral("System"));
    settings.setValue(QStringLiteral("FallbackIconThemeName"), fallbackIconThemeName_);
    settings.setValue(QStringLiteral("Terminal"), terminal_);
    settings.setValue(QStringLiteral("Archiver"), archiver_);
    settings.setValue(QStringLiteral("SIUnit"), siUnit_);

    settings.setValue(QStringLiteral("OnlyUserTemplates"), onlyUserTemplates_);
    settings.setValue(QStringLiteral("TemplateTypeOnce"), templateTypeOnce_);
    settings.setValue(QStringLiteral("TemplateRunApp"), templateRunApp_);

    settings.endGroup();

    settings.beginGroup(QStringLiteral("Behavior"));
    settings.setValue(QStringLiteral("SingleWindowMode"), singleWindowMode_);
    settings.setValue(QStringLiteral("BookmarkOpenMethod"),
                      QString::fromUtf8(bookmarkOpenMethodToString(bookmarkOpenMethod_)));
    settings.setValue(QStringLiteral("PreservePermissions"), preservePermissions_);
    // settings for use with libfm
    settings.setValue(QStringLiteral("UseTrash"), useTrash_);
    settings.setValue(QStringLiteral("SingleClick"), singleClick_);
    settings.setValue(QStringLiteral("AutoSelectionDelay"), autoSelectionDelay_);
    settings.setValue(QStringLiteral("CtrlRightClick"), ctrlRightClick_);
    settings.setValue(QStringLiteral("ConfirmDelete"), confirmDelete_);
    settings.setValue(QStringLiteral("NoUsbTrash"), noUsbTrash_);
    settings.setValue(QStringLiteral("ConfirmTrash"), confirmTrash_);
    settings.setValue(QStringLiteral("QuickExec"), quickExec_);
    settings.setValue(QStringLiteral("SelectNewFiles"), selectNewFiles_);
    settings.setValue(QStringLiteral("OpenWithDefaultFileManager"), openWithDefaultFileManager_);
    settings.setValue(QStringLiteral("AllSticky"), allSticky_);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Thumbnail"));
    settings.setValue(QStringLiteral("ShowThumbnails"), showThumbnails_);
    settings.setValue(QStringLiteral("MaxThumbnailFileSize"), maxThumbnailFileSize());
    settings.setValue(QStringLiteral("MaxExternalThumbnailFileSize"), maxExternalThumbnailFileSize());
    settings.setValue(QStringLiteral("ThumbnailLocalFilesOnly"), thumbnailLocalFilesOnly());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("FolderView"));
    settings.setValue(QStringLiteral("Mode"), Settings::viewModeToString(viewMode_));
    settings.setValue(QStringLiteral("ShowHidden"), showHidden_);
    settings.setValue(QStringLiteral("SortOrder"), Settings::sortOrderToString(sortOrder_));
    settings.setValue(QStringLiteral("SortColumn"), Settings::sortColumnToString(sortColumn_));
    settings.setValue(QStringLiteral("SortFolderFirst"), sortFolderFirst_);
    settings.setValue(QStringLiteral("SortCaseSensitive"), sortCaseSensitive_);
    settings.setValue(QStringLiteral("ShowFilter"), showFilter_);

    settings.setValue(QStringLiteral("BackupAsHidden"), backupAsHidden_);
    settings.setValue(QStringLiteral("ShowFullNames"), showFullNames_);
    settings.setValue(QStringLiteral("ShadowHidden"), shadowHidden_);
    settings.setValue(QStringLiteral("NoItemTooltip"), noItemTooltip_);
    settings.setValue(QStringLiteral("ScrollPerPixel"), scrollPerPixel_);

    // override config in libfm's FmConfig
    settings.setValue(QStringLiteral("BigIconSize"), bigIconSize_);
    settings.setValue(QStringLiteral("SmallIconSize"), smallIconSize_);
    settings.setValue(QStringLiteral("SidePaneIconSize"), sidePaneIconSize_);
    settings.setValue(QStringLiteral("ThumbnailIconSize"), thumbnailIconSize_);

    settings.setValue(QStringLiteral("FolderViewCellMargins"), folderViewCellMargins_);

    // detailed list columns
    settings.setValue(QStringLiteral("CustomColumnWidths"), customColumnWidths_);
    QList<int> columns = getHiddenColumns();
    std::sort(columns.begin(), columns.end());
    QList<QVariant> hiddenColumns;
    for (int i = 0; i < columns.size(); ++i) {
        hiddenColumns << QVariant(columns.at(i));
    }
    settings.setValue(QStringLiteral("HiddenColumns"), hiddenColumns);

    settings.endGroup();

    settings.beginGroup(QStringLiteral("Places"));
    QStringList hiddenPlacesList(hiddenPlaces_.begin(), hiddenPlaces_.end());
    settings.setValue(QStringLiteral("HiddenPlaces"), hiddenPlacesList);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Window"));
    settings.setValue(QStringLiteral("FixedWidth"), fixedWindowWidth_);
    settings.setValue(QStringLiteral("FixedHeight"), fixedWindowHeight_);
    settings.setValue(QStringLiteral("LastWindowWidth"), lastWindowWidth_);
    settings.setValue(QStringLiteral("LastWindowHeight"), lastWindowHeight_);
    settings.setValue(QStringLiteral("LastWindowX"), lastWindowX_);
    settings.setValue(QStringLiteral("LastWindowY"), lastWindowY_);
    settings.setValue(QStringLiteral("LastWindowMaximized"), lastWindowMaximized_);
    settings.setValue(QStringLiteral("RememberWindowSize"), rememberWindowSize_);
    settings.setValue(QStringLiteral("AlwaysShowTabs"), alwaysShowTabs_);
    settings.setValue(QStringLiteral("ShowTabClose"), showTabClose_);
    settings.setValue(QStringLiteral("SwitchToNewTab"), switchToNewTab_);
    settings.setValue(QStringLiteral("ReopenLastTabs"), reopenLastTabs_);
    settings.setValue(QStringLiteral("TabPaths"), tabPaths_);
    settings.setValue(QStringLiteral("SplitViewTabsNum"), splitViewTabsNum_);
    settings.setValue(QStringLiteral("SplitterPos"), splitterPos_);
    settings.setValue(QStringLiteral("SidePaneVisible"), sidePaneVisible_);
    settings.setValue(QStringLiteral("SidePaneMode"), QString::fromUtf8(sidePaneModeToString(sidePaneMode_)));
    settings.setValue(QStringLiteral("ShowMenuBar"), showMenuBar_);
    settings.setValue(QStringLiteral("SplitView"), splitView_);
    settings.setValue(QStringLiteral("PathBarButtons"), pathBarButtons_);
    settings.endGroup();

    // save per-folder settings
    Panel::FolderConfig::saveCache();

    settings.beginGroup(QStringLiteral("Search"));
    settings.setValue(QStringLiteral("searchNameCaseInsensitive"), searchNameCaseInsensitive_);
    settings.setValue(QStringLiteral("searchContentCaseInsensitive"), searchContentCaseInsensitive_);
    settings.setValue(QStringLiteral("searchNameRegexp"), searchNameRegexp_);
    settings.setValue(QStringLiteral("searchContentRegexp"), searchContentRegexp_);
    settings.setValue(QStringLiteral("searchRecursive"), searchRecursive_);
    settings.setValue(QStringLiteral("searchhHidden"), searchhHidden_);
    settings.setValue(QStringLiteral("MaxSearchHistory"), maxSearchHistory_);
    settings.setValue(QStringLiteral("NamePatterns"), namePatterns_);
    settings.setValue(QStringLiteral("ContentPatterns"), contentPatterns_);
    settings.endGroup();

    return true;
}

void Settings::clearSearchHistory() {
    namePatterns_.clear();
    contentPatterns_.clear();
}

void Settings::setMaxSearchHistory(int max) {
    maxSearchHistory_ = std::max(max, 0);
    if (maxSearchHistory_ == 0) {
        namePatterns_.clear();
        contentPatterns_.clear();
    }
    else {
        while (namePatterns_.size() > maxSearchHistory_) {
            namePatterns_.removeLast();
        }
        while (contentPatterns_.size() > maxSearchHistory_) {
            contentPatterns_.removeLast();
        }
    }
}

void Settings::addNamePattern(const QString& pattern) {
    if (maxSearchHistory_ == 0 ||
        pattern.isEmpty()
        // "*" is too trivial with a regex search
        || (searchNameRegexp_ && pattern == QLatin1String("*"))) {
        return;
    }
    namePatterns_.removeOne(pattern);
    namePatterns_.prepend(pattern);
    while (namePatterns_.size() > maxSearchHistory_) {
        namePatterns_.removeLast();
    }
}

void Settings::addContentPattern(const QString& pattern) {
    if (maxSearchHistory_ == 0 || pattern.isEmpty() || (searchContentRegexp_ && pattern == QLatin1String("*"))) {
        return;
    }
    contentPatterns_.removeOne(pattern);
    contentPatterns_.prepend(pattern);
    while (contentPatterns_.size() > maxSearchHistory_) {
        contentPatterns_.removeLast();
    }
}

const QList<int>& Settings::iconSizes(IconType type) {
    static const QList<int> sizes_big = {96, 72, 64, 48, 32};
    static const QList<int> sizes_thumbnail = {256, 224, 192, 160, 128, 96, 64};
    static const QList<int> sizes_small = {48, 32, 24, 22, 16};
    switch (type) {
        case Big:
            return sizes_big;
            break;
        case Thumbnail:
            return sizes_thumbnail;
            break;
        case Small:
        default:
            return sizes_small;
            break;
    }
}

// String conversion member functions
const char* Settings::viewModeToString(Panel::FolderView::ViewMode value) {
    const char* ret;
    switch (value) {
        case Panel::FolderView::IconMode:
        default:
            ret = "icon";
            break;
        case Panel::FolderView::CompactMode:
            ret = "compact";
            break;
        case Panel::FolderView::DetailedListMode:
            ret = "detailed";
            break;
        case Panel::FolderView::ThumbnailMode:
            ret = "thumbnail";
            break;
    }
    return ret;
}

const char* Settings::sortOrderToString(Qt::SortOrder order) {
    return (order == Qt::DescendingOrder ? "descending" : "ascending");
}

const char* Settings::sortColumnToString(Panel::FolderModel::ColumnId value) {
    const char* ret;
    switch (value) {
        case Panel::FolderModel::ColumnFileName:
        default:
            ret = "name";
            break;
        case Panel::FolderModel::ColumnFileType:
            ret = "type";
            break;
        case Panel::FolderModel::ColumnFileSize:
            ret = "size";
            break;
        case Panel::FolderModel::ColumnFileMTime:
            ret = "mtime";
            break;
        case Panel::FolderModel::ColumnFileCrTime:
            ret = "crtime";
            break;
        case Panel::FolderModel::ColumnFileDTime:
            ret = "dtime";
            break;
        case Panel::FolderModel::ColumnFileOwner:
            ret = "owner";
            break;
        case Panel::FolderModel::ColumnFileGroup:
            ret = "group";
            break;
    }
    return ret;
}

const char* Settings::sidePaneModeToString(Panel::SidePane::Mode value) {
    const char* ret;
    switch (value) {
        case Panel::SidePane::ModePlaces:
        default:
            ret = "places";
            break;
        case Panel::SidePane::ModeDirTree:
            ret = "dirtree";
            break;
        case Panel::SidePane::ModeNone:
            ret = "none";
            break;
    }
    return ret;
}

int Settings::toIconSize(int size, IconType type) const {
    const QList<int>& sizes = iconSizes(type);
    for (const auto& s : sizes) {
        if (size >= s) {
            return s;
        }
    }
    return sizes.back();
}

static const char* bookmarkOpenMethodToString(OpenDirTargetType value) {
    switch (value) {
        case OpenInCurrentTab:
        default:
            return "current_tab";
        case OpenInNewTab:
            return "new_tab";
        case OpenInNewWindow:
            return "new_window";
        case OpenInLastActiveWindow:
            return "last_window";
    }
    return "";
}

OpenDirTargetType FolderSettings::bookmarkOpenMethodFromString(const QString& str) {
    if (str == QStringLiteral("new_tab")) {
        return OpenInNewTab;
    }
    else if (str == QStringLiteral("new_window")) {
        return OpenInNewWindow;
    }
    else if (str == QStringLiteral("last_window")) {
        return OpenInLastActiveWindow;
    }
    return OpenInCurrentTab;
}

Panel::FolderView::ViewMode FolderSettings::viewModeFromString(const QString& str) {
    Panel::FolderView::ViewMode ret;
    if (str == QLatin1String("icon")) {
        ret = Panel::FolderView::IconMode;
    }
    else if (str == QLatin1String("compact")) {
        ret = Panel::FolderView::CompactMode;
    }
    else if (str == QLatin1String("detailed")) {
        ret = Panel::FolderView::DetailedListMode;
    }
    else if (str == QLatin1String("thumbnail")) {
        ret = Panel::FolderView::ThumbnailMode;
    }
    else {
        ret = Panel::FolderView::DetailedListMode;
    }
    return ret;
}

Qt::SortOrder FolderSettings::sortOrderFromString(const QString& str) {
    return (str == QLatin1String("descending") ? Qt::DescendingOrder : Qt::AscendingOrder);
}

Panel::FolderModel::ColumnId FolderSettings::sortColumnFromString(const QString& str) {
    Panel::FolderModel::ColumnId ret;
    if (str == QLatin1String("name")) {
        ret = Panel::FolderModel::ColumnFileName;
    }
    else if (str == QLatin1String("type")) {
        ret = Panel::FolderModel::ColumnFileType;
    }
    else if (str == QLatin1String("size")) {
        ret = Panel::FolderModel::ColumnFileSize;
    }
    else if (str == QLatin1String("mtime")) {
        ret = Panel::FolderModel::ColumnFileMTime;
    }
    else if (str == QLatin1String("crtime")) {
        ret = Panel::FolderModel::ColumnFileCrTime;
    }
    else if (str == QLatin1String("dtime")) {
        ret = Panel::FolderModel::ColumnFileDTime;
    }
    else if (str == QLatin1String("owner")) {
        ret = Panel::FolderModel::ColumnFileOwner;
    }
    else if (str == QLatin1String("group")) {
        ret = Panel::FolderModel::ColumnFileGroup;
    }
    else {
        ret = Panel::FolderModel::ColumnFileName;
    }
    return ret;
}

Panel::SidePane::Mode FolderSettings::sidePaneModeFromString(const QString& str) {
    Panel::SidePane::Mode ret;
    if (str == QLatin1String("none")) {
        ret = Panel::SidePane::ModeNone;
    }
    else if (str == QLatin1String("dirtree")) {
        ret = Panel::SidePane::ModeDirTree;
    }
    else {
        ret = Panel::SidePane::ModePlaces;
    }
    return ret;
}

void Settings::setTerminal(QString terminalCommand) {
    terminal_ = terminalCommand;
    Panel::setDefaultTerminal(terminal_.toStdString());
}

// per-folder settings
FolderSettings Settings::loadFolderSettings(const Panel::FilePath& path) const {
    FolderSettings settings;
    Panel::FolderConfig cfg(path);
    bool customized = !cfg.isEmpty();
    Panel::FilePath inheritedPath;
    if (!customized && !path.isParentOf(path)) {  // WARNING: menu://applications/ is its own parent
        inheritedPath = path.parent();
        while (inheritedPath.isValid()) {
            Panel::GErrorPtr err;
            cfg.close(err);
            cfg.open(inheritedPath);
            if (!cfg.isEmpty()) {
                bool recursive;
                if (cfg.getBoolean("Recursive", &recursive) && recursive) {
                    break;
                }
            }
            if (inheritedPath.isParentOf(inheritedPath)) {
                inheritedPath = Panel::FilePath();  // invalidate it
                break;
            }
            inheritedPath = inheritedPath.parent();
        }
    }
    if (!customized && !inheritedPath.isValid()) {
        // the folder is not customized and does not inherit settings; use the general settings
        settings.setSortOrder(sortOrder());
        settings.setSortColumn(sortColumn());
        settings.setViewMode(viewMode());
        settings.setShowHidden(showHidden());
        settings.setSortFolderFirst(sortFolderFirst());
        settings.setSortCaseSensitive(sortCaseSensitive());
    }
    else {
        // either the folder is customized or it inherits settings; load folder-specific settings
        if (!inheritedPath.isValid()) {
            settings.setCustomized(true);
        }
        else {
            settings.seInheritedPath(inheritedPath);
        }

        char* str;
        // load sorting
        str = cfg.getString("SortOrder");
        if (str != nullptr) {
            settings.setSortOrder(FolderSettings::sortOrderFromString(QString::fromUtf8(str)));
            g_free(str);
        }

        str = cfg.getString("SortColumn");
        if (str != nullptr) {
            settings.setSortColumn(FolderSettings::sortColumnFromString(QString::fromUtf8(str)));
            g_free(str);
        }

        str = cfg.getString("ViewMode");
        if (str != nullptr) {
            // set view mode
            settings.setViewMode(FolderSettings::viewModeFromString(QString::fromUtf8(str)));
            g_free(str);
        }

        bool show_hidden;
        if (cfg.getBoolean("ShowHidden", &show_hidden)) {
            settings.setShowHidden(show_hidden);
        }

        bool folder_first;
        if (cfg.getBoolean("SortFolderFirst", &folder_first)) {
            settings.setSortFolderFirst(folder_first);
        }

        bool case_sensitive;
        if (cfg.getBoolean("SortCaseSensitive", &case_sensitive)) {
            settings.setSortCaseSensitive(case_sensitive);
        }

        bool recursive;
        if (cfg.getBoolean("Recursive", &recursive)) {
            settings.setRecursive(recursive);
        }
    }
    return settings;
}

void Settings::saveFolderSettings(const Panel::FilePath& path, const FolderSettings& settings) {
    if (path) {
        // ensure that we have the libfm dir
        QString dirName = xdgUserConfigDir() + QStringLiteral("/libfm");
        QString error;
        FsQt::makeDirParents(dirName, error);  // if libfm config dir does not exist, create it

        Panel::FolderConfig cfg(path);
        cfg.setString("SortOrder", sortOrderToString(settings.sortOrder()));
        cfg.setString("SortColumn", sortColumnToString(settings.sortColumn()));
        cfg.setString("ViewMode", viewModeToString(settings.viewMode()));
        cfg.setBoolean("ShowHidden", settings.showHidden());
        cfg.setBoolean("SortFolderFirst", settings.sortFolderFirst());
        cfg.setBoolean("SortCaseSensitive", settings.sortCaseSensitive());
        cfg.setBoolean("Recursive", settings.recursive());
    }
}

void Settings::clearFolderSettings(const Panel::FilePath& path) const {
    if (path) {
        Panel::FolderConfig cfg(path);
        cfg.purge();
    }
}

}  // namespace Oneg4FM
