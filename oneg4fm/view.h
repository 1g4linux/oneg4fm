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

#ifndef PCMANFM_FOLDERVIEW_H
#define PCMANFM_FOLDERVIEW_H

#include "panel/panel.h"

#include <QTimer>

class QEvent;

namespace Fm {
class FileMenu;
class FolderMenu;
}  // namespace Fm

namespace Oneg4FM {

class Settings;

class View : public Panel::FolderView {
    Q_OBJECT
   public:
    explicit View(Panel::FolderView::ViewMode _mode = IconMode, QWidget* parent = nullptr);
    ~View() override;

    void updateFromSettings(Settings& settings);
    void setViewMode(Panel::FolderView::ViewMode mode);
    void setModel(Panel::ProxyFolderModel* _model);

    QSize getMargins() const { return Panel::FolderView::getMargins(); }
    void setMargins(QSize size) { Panel::FolderView::setMargins(size); }

   protected Q_SLOTS:
    void onNewWindow();
    void onNewTab();
    void onOpenInTerminal();
    void onCalculateBlake3();
    void onOpenInHexEditor();
    void onDisassembleWithCapstone();
    void onSearch();

   protected:
    void onFileClicked(int type, const std::shared_ptr<const Panel::FileInfo>& fileInfo) override;
    void prepareFileMenu(Panel::FileMenu* menu) override;
    void prepareFolderMenu(Panel::FolderMenu* menu) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    void launchFiles(Panel::FileInfoList files, bool inNewTabs = false);
    void openFolderAndSelectFile(const std::shared_ptr<const Panel::FileInfo>& fileInfo, bool inNewTab = false);
    void startArchiveCompression(const QStringList& paths);
    void startArchiveExtraction(const QString& archivePath, const QString& destinationDir);
    static void removeLibfmArchiverActions(Panel::FileMenu* menu);

    void setupThumbnailHooks();
    void scheduleThumbnailPrefetch();
    void requestVisibleThumbnails();

   private:
    QTimer thumbnailPrefetchTimer_;
};

}  // namespace Oneg4FM
#endif  // PCMANFM_FOLDERVIEW_H
