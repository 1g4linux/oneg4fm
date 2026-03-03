/*
 * Application lifecycle/session handling.
 * oneg4fm/application_lifecycle.cpp
 */

#include "application.h"

#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QDir>
#include <QDBusConnection>
#include <QFileSystemWatcher>
#include <QLibraryInfo>
#include <QLocale>
#include <QSocketNotifier>
#include <QStandardPaths>

#include "mainwindow.h"
#include "../src/ui/fsqt.h"

namespace Oneg4FM {

static int sigterm_fd[2];

// Async-signal-safe handler
static void sigtermHandler(int) {
    char c = 1;
    // We ignore the return value because we are in a signal handler
    // and cannot handle errors effectively/safely here anyway.
    if (::write(sigterm_fd[0], &c, sizeof(c))) {
    }
}

void Application::initWatch() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString filePath = configDir + QStringLiteral("/user-dirs.dirs");
    QByteArray data;
    QString error;
    if (Oneg4FM::FsQt::readFile(filePath, data, error)) {
        userDirsFile_ = filePath;
    }
    else {
        qDebug() << Q_FUNC_INFO << "Could not read:" << filePath << error;
        userDirsFile_.clear();
    }

    userDirsWatcher_ = new QFileSystemWatcher(this);
    if (!userDirsFile_.isEmpty()) {
        userDirsWatcher_->addPath(userDirsFile_);
    }
}

void Application::init() {
    // load Qt translations
    if (qtTranslator.load(QStringLiteral("qt_") + QLocale::system().name(),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        installTranslator(&qtTranslator);
    }

    // load libfm-qt translations
    installTranslator(libFm_.translator());

    // load oneg4fm translations
    if (translator.load(QStringLiteral("oneg4fm_") + QLocale::system().name(),
                        QStringLiteral(PCMANFM_DATA_DIR) + QStringLiteral("/translations"))) {
        installTranslator(&translator);
    }
}

int Application::exec() {
    if (!parseCommandLineArgs()) {
        return startupExitCode_;
    }

    if (daemonMode_) {
        // keep running even when all windows are closed
        setQuitOnLastWindowClosed(false);
    }

    return QCoreApplication::exec();
}

void Application::stopUserDirsWatcher() {
    if (!userDirsWatcher_) {
        return;
    }

    const QStringList watchedFiles = userDirsWatcher_->files();
    if (!watchedFiles.isEmpty()) {
        userDirsWatcher_->removePaths(watchedFiles);
    }

    const QStringList watchedDirectories = userDirsWatcher_->directories();
    if (!watchedDirectories.isEmpty()) {
        userDirsWatcher_->removePaths(watchedDirectories);
    }

    delete userDirsWatcher_;
    userDirsWatcher_ = nullptr;
    userDirsFile_.clear();
}

void Application::stopPrimaryDbusServices() {
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.unregisterObject(QStringLiteral("/org/freedesktop/FileManager1"));
    dbus.unregisterObject(QStringLiteral("/Application"));
    dbus.unregisterService(QStringLiteral("org.freedesktop.FileManager1"));
    dbus.unregisterService(QStringLiteral(ONEG4FM_DBUS_APP_SERVICE));
}

void Application::performShutdownSequence() {
    if (shutdownSequenceDone_) {
        return;
    }
    shutdownSequenceDone_ = true;

    // Explicit shutdown order: persist state, stop watchers, stop DBus ownership.
    settings_.save();
    stopUserDirsWatcher();
    stopPrimaryDbusServices();
}

void Application::onAboutToQuit() {
    qDebug("aboutToQuit");
    performShutdownSequence();
}

void Application::cleanPerFolderConfig() {
    // flush the in-memory per-folder config so we know all currently customized folders
    Panel::FolderConfig::saveCache();

    // then remove non-existent native folders from the list of custom folders
    QByteArray perFolderConfig =
        (settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf")).toLocal8Bit();

    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, perFolderConfig.constData(), G_KEY_FILE_NONE, nullptr)) {
        bool removed = false;
        gchar** groups = g_key_file_get_groups(kf, nullptr);

        for (int i = 0; groups[i] != nullptr; ++i) {
            const gchar* g = groups[i];

            // only clean native paths, leave virtual paths alone
            if (Panel::FilePath::fromPathStr(g).isNative() && !QDir(QString::fromUtf8(g)).exists()) {
                g_key_file_remove_group(kf, g, nullptr);
                removed = true;
            }
        }

        g_strfreev(groups);

        if (removed) {
            g_key_file_save_to_file(kf, perFolderConfig.constData(), nullptr);
        }
    }
    g_key_file_free(kf);
}

void Application::onLastWindowClosed() {
    // kept for future use if we need custom behavior when the last window closes
}

void Application::onSaveStateRequest(QSessionManager& /*manager*/) {
    // session management is not used; method kept for potential future integration
}

void Application::installSigtermHandler() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigterm_fd) == 0) {
        auto* notifier = new QSocketNotifier(sigterm_fd[1], QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &Application::onSigtermNotified);

        struct sigaction action;
        action.sa_handler = sigtermHandler;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;

        if (::sigaction(SIGTERM, &action, nullptr) != 0) {
            qWarning("Couldn't install SIGTERM handler");
        }
    }
    else {
        qWarning("Couldn't create SIGTERM socketpair");
    }
}

void Application::onSigtermNotified() {
    auto* notifier = qobject_cast<QSocketNotifier*>(sender());
    if (!notifier) {
        return;
    }

    notifier->setEnabled(false);

    char c;
    if (::read(sigterm_fd[1], &c, sizeof(c))) {
    }

    // close all main windows cleanly before quitting
    const auto windows = topLevelWidgets();
    for (auto* win : windows) {
        if (auto* mainWindow = qobject_cast<MainWindow*>(win)) {
            mainWindow->close();
        }
    }

    quit();
}

}  // namespace Oneg4FM
