/*
 * Application implementation for oneg4fm
 * oneg4fm/application.cpp
 */

#include "application.h"

// Panel (libfm-qt fork) Headers
#include "panel/panel.h"

// Qt Headers
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QIcon>

// Local Headers
#include "applicationadaptor.h"
#include "applicationadaptorfreedesktopfilemanager.h"
#include "application_startup_plan.h"

namespace Oneg4FM {

#ifndef ONEG4FM_DBUS_APP_SERVICE
#error "ONEG4FM_DBUS_APP_SERVICE must be defined by the build system"
#endif

#ifndef ONEG4FM_DBUS_APP_INTERFACE
#error "ONEG4FM_DBUS_APP_INTERFACE must be defined by the build system"
#endif

static constexpr const char* serviceName = ONEG4FM_DBUS_APP_SERVICE;
static constexpr const char* ifaceName = ONEG4FM_DBUS_APP_INTERFACE;

//-----------------------------------------------------------------------------
// ProxyStyle
//-----------------------------------------------------------------------------

int ProxyStyle::styleHint(StyleHint hint,
                          const QStyleOption* option,
                          const QWidget* widget,
                          QStyleHintReturn* returnData) const {
    auto* app = qobject_cast<Application*>(qApp);

    if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick && app) {
        if (app->settings().singleClick()) {
            return true;
        }
        // fall back to the base style when single-click is disabled
        return QCommonStyle::styleHint(hint, option, widget, returnData);
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

//-----------------------------------------------------------------------------
// Application Class
//-----------------------------------------------------------------------------

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv),
      libFm_(),
      settings_(),
      profileName_(QStringLiteral("default")),
      daemonMode_(false),
      preferencesDialog_(),
      editBookmarksialog_(),
      userDirsWatcher_(nullptr),
      openingLastTabs_(false),
      shutdownSequenceDone_(false) {
    argc_ = argc;
    argv_ = argv;

    setApplicationVersion(QStringLiteral(PCMANFM_QT_VERSION));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("oneg4fm")));

    QDBusConnection dbus = QDBusConnection::sessionBus();

    // Attempt to register service. If successful, we are the primary instance.
    if (dbus.registerService(QLatin1String(serviceName))) {
        isPrimaryInstance = true;

        setStyle(new ProxyStyle());

        new ApplicationAdaptor(this);
        dbus.registerObject(QStringLiteral("/Application"), this);

        connect(this, &Application::aboutToQuit, this, &Application::onAboutToQuit);

        // aboutToQuit() is not emitted on SIGTERM, so install a handler that shuts down cleanly
        installSigtermHandler();

        // also provide the standard org.freedesktop.FileManager1 interface if possible
        const QString fileManagerService = QStringLiteral("org.freedesktop.FileManager1");

        if (auto* iface = dbus.interface()) {
            connect(iface, &QDBusConnectionInterface::serviceRegistered, this,
                    [this, fileManagerService](const QString& service) {
                        if (fileManagerService == service) {
                            QDBusConnection dbusSession = QDBusConnection::sessionBus();
                            if (auto* ifaceConn = dbusSession.interface()) {
                                disconnect(ifaceConn, &QDBusConnectionInterface::serviceRegistered, this, nullptr);
                            }
                            new ApplicationAdaptorFreeDesktopFileManager(this);
                            if (!dbusSession.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), this)) {
                                qDebug() << "Can't register /org/freedesktop/FileManager1:"
                                         << dbusSession.lastError().message();
                            }
                        }
                    });
            iface->registerService(fileManagerService, QDBusConnectionInterface::QueueService);
        }
    }
    else {
        // another instance already owns org.oneg4fm.oneg4fm, this one acts as a client
        isPrimaryInstance = false;
    }
}

Application::~Application() {}

bool Application::parseCommandLineArgs() {
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption profileOption({QStringLiteral("p"), QStringLiteral("profile")},
                                     tr("Name of configuration profile"), tr("PROFILE"));
    parser.addOption(profileOption);

    QCommandLineOption daemonOption({QStringLiteral("d"), QStringLiteral("daemon-mode")},
                                    tr("Run oneg4fm as a daemon"));
    parser.addOption(daemonOption);

    QCommandLineOption quitOption({QStringLiteral("q"), QStringLiteral("quit")}, tr("Quit oneg4fm"));
    parser.addOption(quitOption);

    QCommandLineOption newWindowOption({QStringLiteral("n"), QStringLiteral("new-window")}, tr("Open new window"));
    parser.addOption(newWindowOption);

    QCommandLineOption findFilesOption({QStringLiteral("f"), QStringLiteral("find-files")},
                                       tr("Open Find Files utility"));
    parser.addOption(findFilesOption);

    QCommandLineOption showPrefOption(QStringLiteral("show-pref"),
                                      tr("Open Preferences dialog on the page with the specified name") +
                                          QStringLiteral("\n") + tr("NAME") +
                                          QStringLiteral("=(behavior|display|ui|thumbnail|volume|advanced)"),
                                      tr("NAME"));
    parser.addOption(showPrefOption);

    parser.addPositionalArgument(QStringLiteral("files"), tr("Files or directories to open"), tr("[FILE1, FILE2,...]"));

    parser.process(arguments());
    Startup::CliIntent intent;
    intent.profileName = profileName_;
    intent.daemonMode = parser.isSet(daemonOption);
    intent.quit = parser.isSet(quitOption);
    intent.findFiles = parser.isSet(findFilesOption);
    intent.showPreferences = parser.isSet(showPrefOption);
    if (intent.showPreferences) {
        intent.showPreferencesPage = parser.value(showPrefOption);
    }
    intent.newWindow = parser.isSet(newWindowOption);
    intent.positionalPaths = parser.positionalArguments();
    if (parser.isSet(profileOption)) {
        intent.profileName = parser.value(profileOption);
    }

    if (isPrimaryInstance) {
        const Startup::PrimaryStartupPlan plan = Startup::planForPrimaryInstance(intent, QDir::currentPath());
        daemonMode_ = plan.daemonMode;
        profileName_ = plan.profileName;

        // load global application configuration
        settings_.load(profileName_);
        // Disable libfm-qt archiver integration; compression is handled in-process.
        Panel::Archiver::setDefaultArchiver(nullptr);

        // initialize per-folder config backed by dir-settings.conf
        const QString perFolderConfigFile = settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf");
        Panel::FolderConfig::init(perFolderConfigFile.toLocal8Bit().constData());

        if (settings_.useFallbackIconTheme()) {
            QIcon::setThemeName(settings_.fallbackIconThemeName());
        }

        if (plan.action == Startup::PlannedAction::FindFiles) {
            findFiles(plan.findFilesPaths);
        }
        else if (plan.action == Startup::PlannedAction::ShowPreferences) {
            preferences(plan.preferencesPage);
        }
        else if (plan.action == Startup::PlannedAction::LaunchFiles && plan.shouldLaunch) {
            launchFiles(plan.launch.cwd, plan.launch.paths, plan.launch.inNewWindow, plan.launch.reopenLastTabs);
        }

        return plan.keepRunning;
    }
    // secondary instance, forward the request to the primary via DBus
    const Startup::SecondaryStartupPlan plan = Startup::planForSecondaryInstance(intent, QDir::currentPath());
    QDBusConnection dbus = QDBusConnection::sessionBus();
    QDBusInterface iface(QLatin1String(serviceName), QStringLiteral("/Application"), QLatin1String(ifaceName), dbus,
                         this);

    switch (plan.action) {
        case Startup::PlannedAction::Quit:
            iface.call(QStringLiteral("quit"));
            break;
        case Startup::PlannedAction::FindFiles:
            iface.call(QStringLiteral("findFiles"), plan.findFilesPaths);
            break;
        case Startup::PlannedAction::ShowPreferences:
            iface.call(QStringLiteral("preferences"), plan.preferencesPage);
            break;
        case Startup::PlannedAction::LaunchFiles:
            iface.call(QStringLiteral("launchFiles"), plan.launch.cwd, plan.launch.paths, plan.launch.inNewWindow,
                       plan.launch.reopenLastTabs);
            break;
    }

    return plan.keepRunning;
}

}  // namespace Oneg4FM
