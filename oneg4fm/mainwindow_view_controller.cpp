/*
 * Main window file-view controller implementation
 * oneg4fm/mainwindow_view_controller.cpp
 */

#include "mainwindow_view_controller.h"

namespace Oneg4FM::MainWindowViewController {

namespace {

bool hasPage(const Context& context) {
    return context.hasCurrentPage();
}

}  // namespace

void setViewMode(Context& context, ViewMode mode) {
    if (!hasPage(context)) {
        return;
    }
    context.applyViewMode(mode);
}

void sortByColumn(Context& context, SortColumn column) {
    if (!hasPage(context)) {
        return;
    }
    context.applySort(column, context.currentSortOrder());
}

void sortByOrder(Context& context, Qt::SortOrder order) {
    if (!hasPage(context)) {
        return;
    }
    context.applySort(context.currentSortColumn(), order);
}

void setSortCaseSensitive(Context& context, bool enabled) {
    if (!hasPage(context)) {
        return;
    }
    context.applySortCaseSensitive(enabled);
}

void setSortFolderFirst(Context& context, bool enabled) {
    if (!hasPage(context)) {
        return;
    }
    context.applySortFolderFirst(enabled);
}

void setFilterBarsPersistent(Context& context, bool enabled) {
    context.setFilterBarsPersistent(enabled);
}

void clearFilters(Context& context) {
    context.clearFiltersInCurrentWindow();
}

void showFilterBar(Context& context) {
    if (!hasPage(context)) {
        return;
    }
    context.showCurrentFilterBar();
}

void handlePageStateChange(Context& context, ViewFrame* frame, bool setFocusForActivePage) {
    if (!frame) {
        return;
    }

    if (frame == context.activeViewFrame()) {
        context.projectCurrentPageUi(setFocusForActivePage);
    }
    else {
        context.syncPathBarForFrame(frame);
    }
}

}  // namespace Oneg4FM::MainWindowViewController
