/*
 * Startup planning helpers for Application command-line routing.
 */

#include "application_startup_plan.h"

namespace Oneg4FM::Startup {

PrimaryStartupPlan planForPrimaryInstance(const CliIntent& intent, const QString& currentWorkingDir) {
    PrimaryStartupPlan plan;
    plan.profileName = intent.profileName;
    plan.daemonMode = intent.daemonMode;

    if (intent.findFiles) {
        plan.action = PlannedAction::FindFiles;
        plan.findFilesPaths = intent.positionalPaths;
        return plan;
    }

    if (intent.showPreferences) {
        plan.action = PlannedAction::ShowPreferences;
        plan.preferencesPage = intent.showPreferencesPage;
        return plan;
    }

    plan.action = PlannedAction::LaunchFiles;
    plan.launch.cwd = currentWorkingDir;
    plan.launch.paths = intent.positionalPaths;
    plan.launch.inNewWindow = intent.newWindow;

    if (plan.launch.paths.isEmpty() && !plan.daemonMode) {
        plan.launch.reopenLastTabs = true;
        plan.launch.paths.push_back(currentWorkingDir);
    }

    plan.shouldLaunch = !plan.launch.paths.isEmpty();
    return plan;
}

SecondaryStartupPlan planForSecondaryInstance(const CliIntent& intent, const QString& currentWorkingDir) {
    SecondaryStartupPlan plan;

    if (intent.quit) {
        plan.action = PlannedAction::Quit;
        return plan;
    }

    if (intent.findFiles) {
        plan.action = PlannedAction::FindFiles;
        plan.findFilesPaths = intent.positionalPaths;
        return plan;
    }

    if (intent.showPreferences) {
        plan.action = PlannedAction::ShowPreferences;
        plan.preferencesPage = intent.showPreferencesPage;
        return plan;
    }

    plan.action = PlannedAction::LaunchFiles;
    plan.launch.cwd = currentWorkingDir;
    plan.launch.paths = intent.positionalPaths;
    plan.launch.inNewWindow = intent.newWindow;

    if (plan.launch.paths.isEmpty()) {
        plan.launch.reopenLastTabs = true;
        plan.launch.paths.push_back(currentWorkingDir);
    }

    return plan;
}

}  // namespace Oneg4FM::Startup
