/*
 * Main window view command dispatcher implementation
 * oneg4fm/mainwindow_view_commands.cpp
 */

#include "mainwindow_view_commands.h"

#include <QtGlobal>

namespace Oneg4FM::MainWindowViewCommands {

namespace {

class Command {
   public:
    virtual ~Command() = default;
    virtual bool canExecute(const Context& context) const = 0;
    virtual void execute(Context& context) const = 0;
};

class IconViewCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.setIconMode(); }
};

class CompactViewCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.setCompactMode(); }
};

class DetailedViewCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.setDetailedMode(); }
};

class ThumbnailViewCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.setThumbnailMode(); }
};

class SortAscendingCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortAscending(); }
};

class SortDescendingCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortDescending(); }
};

class SortByFileNameCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByFileName(); }
};

class SortByMTimeCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByMTime(); }
};

class SortByCrTimeCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByCrTime(); }
};

class SortByDTimeCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByDTime(); }
};

class SortByOwnerCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByOwner(); }
};

class SortByGroupCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByGroup(); }
};

class SortByFileSizeCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByFileSize(); }
};

class SortByFileTypeCommand final : public Command {
   public:
    bool canExecute(const Context& context) const override { return context.hasCurrentPage(); }

    void execute(Context& context) const override { context.sortByFileType(); }
};

const Command& commandForId(Id id) {
    static const IconViewCommand iconView;
    static const CompactViewCommand compactView;
    static const DetailedViewCommand detailedView;
    static const ThumbnailViewCommand thumbnailView;
    static const SortAscendingCommand sortAscending;
    static const SortDescendingCommand sortDescending;
    static const SortByFileNameCommand sortByFileName;
    static const SortByMTimeCommand sortByMTime;
    static const SortByCrTimeCommand sortByCrTime;
    static const SortByDTimeCommand sortByDTime;
    static const SortByOwnerCommand sortByOwner;
    static const SortByGroupCommand sortByGroup;
    static const SortByFileSizeCommand sortByFileSize;
    static const SortByFileTypeCommand sortByFileType;

    switch (id) {
        case Id::IconView:
            return iconView;
        case Id::CompactView:
            return compactView;
        case Id::DetailedView:
            return detailedView;
        case Id::ThumbnailView:
            return thumbnailView;
        case Id::SortAscending:
            return sortAscending;
        case Id::SortDescending:
            return sortDescending;
        case Id::SortByFileName:
            return sortByFileName;
        case Id::SortByMTime:
            return sortByMTime;
        case Id::SortByCrTime:
            return sortByCrTime;
        case Id::SortByDTime:
            return sortByDTime;
        case Id::SortByOwner:
            return sortByOwner;
        case Id::SortByGroup:
            return sortByGroup;
        case Id::SortByFileSize:
            return sortByFileSize;
        case Id::SortByFileType:
            return sortByFileType;
    }

    Q_UNREACHABLE();
}

}  // namespace

bool canExecute(Id id, const Context& context) {
    return commandForId(id).canExecute(context);
}

void execute(Id id, Context& context) {
    const Command& command = commandForId(id);
    if (!command.canExecute(context)) {
        return;
    }
    command.execute(context);
}

}  // namespace Oneg4FM::MainWindowViewCommands
