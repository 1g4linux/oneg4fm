/*
 * File launcher header
 * oneg4fm/launcher.h
 */

#ifndef PCMANFM_LAUNCHER_H
#define PCMANFM_LAUNCHER_H

#include "panel/panel.h"

namespace Oneg4FM {

class MainWindow;

class Launcher : public Panel::FileLauncher {
   public:
    Launcher(MainWindow* mainWindow = nullptr);
    ~Launcher();

    bool hasMainWindow() const { return mainWindow_ != nullptr; }

    void openInNewTab() { openInNewTab_ = true; }

    bool openWithDefaultFileManager() const { return openWithDefaultFileManager_; }
    void setOpenWithDefaultFileManager(bool open) { openWithDefaultFileManager_ = open; }

   protected:
    bool openFolder(GAppLaunchContext* ctx, const Panel::FileInfoList& folderInfos, Panel::GErrorPtr& err) override;

   private:
    MainWindow* mainWindow_;
    bool openInNewTab_;
    bool openWithDefaultFileManager_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_LAUNCHER_H
