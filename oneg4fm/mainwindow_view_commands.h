/*
 * Main window view command dispatcher interface
 * oneg4fm/mainwindow_view_commands.h
 */

#ifndef ONEG4FM_MAINWINDOW_VIEW_COMMANDS_H
#define ONEG4FM_MAINWINDOW_VIEW_COMMANDS_H

namespace Oneg4FM::MainWindowViewCommands {

enum class Id {
    IconView,
    CompactView,
    DetailedView,
    ThumbnailView,
    SortAscending,
    SortDescending,
    SortByFileName,
    SortByMTime,
    SortByCrTime,
    SortByDTime,
    SortByOwner,
    SortByGroup,
    SortByFileSize,
    SortByFileType,
};

class Context {
   public:
    virtual ~Context() = default;

    virtual bool hasCurrentPage() const = 0;
    virtual void setIconMode() = 0;
    virtual void setCompactMode() = 0;
    virtual void setDetailedMode() = 0;
    virtual void setThumbnailMode() = 0;
    virtual void sortAscending() = 0;
    virtual void sortDescending() = 0;
    virtual void sortByFileName() = 0;
    virtual void sortByMTime() = 0;
    virtual void sortByCrTime() = 0;
    virtual void sortByDTime() = 0;
    virtual void sortByOwner() = 0;
    virtual void sortByGroup() = 0;
    virtual void sortByFileSize() = 0;
    virtual void sortByFileType() = 0;
};

bool canExecute(Id id, const Context& context);
void execute(Id id, Context& context);

}  // namespace Oneg4FM::MainWindowViewCommands

#endif  // ONEG4FM_MAINWINDOW_VIEW_COMMANDS_H
