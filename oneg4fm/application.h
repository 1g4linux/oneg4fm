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

#ifndef PCMANFM_APPLICATION_H
#define PCMANFM_APPLICATION_H

#include "panel/panel.h"

#include <QApplication>
#include <QPointer>
#include <QProxyStyle>
#include <QTranslator>
#include <QVector>

#include "settings.h"

class QFileSystemWatcher;

namespace Oneg4FM {

class MainWindow;
class PreferencesDialog;

class ProxyStyle : public QProxyStyle {
    Q_OBJECT
   public:
    ProxyStyle() : QProxyStyle() {}
    virtual ~ProxyStyle() {}
    virtual int styleHint(StyleHint hint,
                          const QStyleOption* option = nullptr,
                          const QWidget* widget = nullptr,
                          QStyleHintReturn* returnData = nullptr) const;
};

class Application : public QApplication {
    Q_OBJECT

   public:
    Application(int& argc, char** argv);
    virtual ~Application();

    void init();
    int exec();

    Settings& settings() { return settings_; }

    Panel::LibFmQt& libFm() { return libFm_; }

    bool openingLastTabs() const { return openingLastTabs_; }

    // public interface exported via dbus
    void launchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs);
    void preferences(const QString& page);
    void editBookmarks();
    void findFiles(QStringList paths = QStringList());
    void ShowFolders(const QStringList& uriList, const QString& startupId);
    void ShowItems(const QStringList& uriList, const QString& startupId);
    void ShowItemProperties(const QStringList& uriList, const QString& startupId);
    void connectToServer();

    void updateFromSettings();

    void openFolderInTerminal(Panel::FilePath path);
    void openFolders(Panel::FileInfoList files);

    QString profileName() { return profileName_; }

    void cleanPerFolderConfig();

   protected Q_SLOTS:
    void onAboutToQuit();
    void onSigtermNotified();

    void onLastWindowClosed();
    void onSaveStateRequest(QSessionManager& manager);

    void onFindFileAccepted();
    void onConnectToServerAccepted();

   protected:
    // virtual bool eventFilter(QObject* watched, QEvent* event);
    bool parseCommandLineArgs();

   private Q_SLOTS:
    void onPropJobFinished();

   private:
    void initWatch();
    void installSigtermHandler();
    void performShutdownSequence();
    void stopUserDirsWatcher();
    void stopPrimaryDbusServices();

    bool isPrimaryInstance;
    Panel::LibFmQt libFm_;
    Settings settings_;
    QString profileName_;
    bool daemonMode_;
    int startupExitCode_;
    QPointer<PreferencesDialog> preferencesDialog_;
    QPointer<Panel::EditBookmarksDialog> editBookmarksialog_;
    QTranslator translator;
    QTranslator qtTranslator;

    QFileSystemWatcher* userDirsWatcher_;
    QString userDirsFile_;
    bool openingLastTabs_;
    bool shutdownSequenceDone_;

    int argc_;
    char** argv_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_APPLICATION_H
