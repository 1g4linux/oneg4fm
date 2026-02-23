/*
 * Startup planning helpers for Application command-line routing.
 */

#ifndef ONEG4FM_APPLICATION_STARTUP_PLAN_H
#define ONEG4FM_APPLICATION_STARTUP_PLAN_H

#include <QString>
#include <QStringList>

namespace Oneg4FM::Startup {

enum class PlannedAction {
    LaunchFiles,
    FindFiles,
    ShowPreferences,
    Quit,
};

struct CliIntent {
    QString profileName;
    bool daemonMode = false;
    bool quit = false;
    bool findFiles = false;
    bool showPreferences = false;
    QString showPreferencesPage;
    bool newWindow = false;
    QStringList positionalPaths;
};

struct LaunchPlan {
    QString cwd;
    QStringList paths;
    bool inNewWindow = false;
    bool reopenLastTabs = false;
};

struct PrimaryStartupPlan {
    QString profileName;
    bool daemonMode = false;
    bool keepRunning = true;
    PlannedAction action = PlannedAction::LaunchFiles;
    LaunchPlan launch;
    bool shouldLaunch = false;
    QStringList findFilesPaths;
    QString preferencesPage;
};

struct SecondaryStartupPlan {
    bool keepRunning = false;
    PlannedAction action = PlannedAction::LaunchFiles;
    LaunchPlan launch;
    QStringList findFilesPaths;
    QString preferencesPage;
};

PrimaryStartupPlan planForPrimaryInstance(const CliIntent& intent, const QString& currentWorkingDir);
SecondaryStartupPlan planForSecondaryInstance(const CliIntent& intent, const QString& currentWorkingDir);

}  // namespace Oneg4FM::Startup

#endif  // ONEG4FM_APPLICATION_STARTUP_PLAN_H
