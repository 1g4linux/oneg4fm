/*
 * Main window file-view controller interface
 * oneg4fm/mainwindow_view_controller.h
 */

#ifndef ONEG4FM_MAINWINDOW_VIEW_CONTROLLER_H
#define ONEG4FM_MAINWINDOW_VIEW_CONTROLLER_H

#include <QtCore/Qt>

namespace Oneg4FM {

class ViewFrame;

namespace MainWindowViewController {

enum class ViewMode {
    Icon,
    Compact,
    Detailed,
    Thumbnail,
};

enum class SortColumn {
    FileName,
    MTime,
    CrTime,
    DTime,
    Owner,
    Group,
    FileSize,
    FileType,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual bool hasCurrentPage() const = 0;
    virtual ViewFrame* activeViewFrame() const = 0;

    virtual void applyViewMode(ViewMode mode) = 0;
    virtual void applySort(SortColumn column, Qt::SortOrder order) = 0;
    virtual Qt::SortOrder currentSortOrder() const = 0;
    virtual SortColumn currentSortColumn() const = 0;
    virtual void applySortCaseSensitive(bool enabled) = 0;
    virtual void applySortFolderFirst(bool enabled) = 0;

    virtual void setFilterBarsPersistent(bool enabled) = 0;
    virtual void clearFiltersInCurrentWindow() = 0;
    virtual void showCurrentFilterBar() = 0;

    virtual void syncPathBarForFrame(ViewFrame* frame) = 0;
    virtual void projectCurrentPageUi(bool setFocus) = 0;
};

void setViewMode(Context& context, ViewMode mode);
void sortByColumn(Context& context, SortColumn column);
void sortByOrder(Context& context, Qt::SortOrder order);
void setSortCaseSensitive(Context& context, bool enabled);
void setSortFolderFirst(Context& context, bool enabled);

void setFilterBarsPersistent(Context& context, bool enabled);
void clearFilters(Context& context);
void showFilterBar(Context& context);

void handlePageStateChange(Context& context, ViewFrame* frame, bool setFocusForActivePage);

}  // namespace MainWindowViewController

}  // namespace Oneg4FM

#endif  // ONEG4FM_MAINWINDOW_VIEW_CONTROLLER_H
