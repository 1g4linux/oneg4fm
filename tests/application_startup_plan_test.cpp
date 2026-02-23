/*
 * tests/application_startup_plan_test.cpp
 */

#include <QTest>

#include "application_startup_plan.h"

class ApplicationStartupPlanTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void primaryLaunchesCurrentDirWhenNoPathsAndNotDaemon();
    void primaryDaemonModeSkipsImplicitLaunch();
    void primaryFindFilesTakesPriorityOverShowPref();
    void primaryShowPreferencesDispatchesPage();
    void secondaryQuitTakesPriority();
    void secondaryLaunchUsesCurrentDirWhenPathsMissing();
    void secondaryLaunchForwardsExplicitPathsAndWindowMode();
    void singleInstanceForwardingMatrixSameProfile();
    void singleInstanceForwardingMatrixDifferentProfile();
};

void ApplicationStartupPlanTest::primaryLaunchesCurrentDirWhenNoPathsAndNotDaemon() {
    Oneg4FM::Startup::CliIntent intent;
    intent.profileName = QStringLiteral("default");

    const auto plan = Oneg4FM::Startup::planForPrimaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QVERIFY(plan.keepRunning);
    QCOMPARE(plan.profileName, QStringLiteral("default"));
    QCOMPARE(plan.daemonMode, false);
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::LaunchFiles);
    QVERIFY(plan.shouldLaunch);
    QCOMPARE(plan.launch.cwd, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.launch.paths, QStringList({QStringLiteral("/tmp/cwd")}));
    QCOMPARE(plan.launch.inNewWindow, false);
    QCOMPARE(plan.launch.reopenLastTabs, true);
}

void ApplicationStartupPlanTest::primaryDaemonModeSkipsImplicitLaunch() {
    Oneg4FM::Startup::CliIntent intent;
    intent.profileName = QStringLiteral("daemon-profile");
    intent.daemonMode = true;

    const auto plan = Oneg4FM::Startup::planForPrimaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QVERIFY(plan.keepRunning);
    QCOMPARE(plan.profileName, QStringLiteral("daemon-profile"));
    QCOMPARE(plan.daemonMode, true);
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::LaunchFiles);
    QCOMPARE(plan.shouldLaunch, false);
    QVERIFY(plan.launch.paths.isEmpty());
    QCOMPARE(plan.launch.reopenLastTabs, false);
}

void ApplicationStartupPlanTest::primaryFindFilesTakesPriorityOverShowPref() {
    Oneg4FM::Startup::CliIntent intent;
    intent.profileName = QStringLiteral("default");
    intent.findFiles = true;
    intent.showPreferences = true;
    intent.showPreferencesPage = QStringLiteral("advanced");
    intent.positionalPaths = {QStringLiteral("/a"), QStringLiteral("/b")};

    const auto plan = Oneg4FM::Startup::planForPrimaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::FindFiles);
    QCOMPARE(plan.findFilesPaths, QStringList({QStringLiteral("/a"), QStringLiteral("/b")}));
    QVERIFY(!plan.shouldLaunch);
    QVERIFY(plan.preferencesPage.isEmpty());
}

void ApplicationStartupPlanTest::primaryShowPreferencesDispatchesPage() {
    Oneg4FM::Startup::CliIntent intent;
    intent.profileName = QStringLiteral("default");
    intent.showPreferences = true;
    intent.showPreferencesPage = QStringLiteral("display");

    const auto plan = Oneg4FM::Startup::planForPrimaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::ShowPreferences);
    QCOMPARE(plan.preferencesPage, QStringLiteral("display"));
    QVERIFY(plan.findFilesPaths.isEmpty());
    QVERIFY(!plan.shouldLaunch);
}

void ApplicationStartupPlanTest::secondaryQuitTakesPriority() {
    Oneg4FM::Startup::CliIntent intent;
    intent.quit = true;
    intent.findFiles = true;
    intent.showPreferences = true;
    intent.showPreferencesPage = QStringLiteral("behavior");
    intent.positionalPaths = {QStringLiteral("/x")};

    const auto plan = Oneg4FM::Startup::planForSecondaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.keepRunning, false);
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::Quit);
    QVERIFY(plan.launch.paths.isEmpty());
    QVERIFY(plan.findFilesPaths.isEmpty());
    QVERIFY(plan.preferencesPage.isEmpty());
}

void ApplicationStartupPlanTest::secondaryLaunchUsesCurrentDirWhenPathsMissing() {
    Oneg4FM::Startup::CliIntent intent;

    const auto plan = Oneg4FM::Startup::planForSecondaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::LaunchFiles);
    QCOMPARE(plan.launch.cwd, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.launch.paths, QStringList({QStringLiteral("/tmp/cwd")}));
    QCOMPARE(plan.launch.inNewWindow, false);
    QCOMPARE(plan.launch.reopenLastTabs, true);
}

void ApplicationStartupPlanTest::secondaryLaunchForwardsExplicitPathsAndWindowMode() {
    Oneg4FM::Startup::CliIntent intent;
    intent.newWindow = true;
    intent.positionalPaths = {QStringLiteral("/a"), QStringLiteral("/b")};

    const auto plan = Oneg4FM::Startup::planForSecondaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::LaunchFiles);
    QCOMPARE(plan.launch.cwd, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.launch.paths, QStringList({QStringLiteral("/a"), QStringLiteral("/b")}));
    QCOMPARE(plan.launch.inNewWindow, true);
    QCOMPARE(plan.launch.reopenLastTabs, false);
}

void ApplicationStartupPlanTest::singleInstanceForwardingMatrixSameProfile() {
    Oneg4FM::Startup::CliIntent intent;
    intent.profileName = QStringLiteral("default");
    intent.newWindow = true;
    intent.positionalPaths = {QStringLiteral("/same-profile/path")};

    const auto plan = Oneg4FM::Startup::planForSecondaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.keepRunning, false);
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::LaunchFiles);
    QCOMPARE(plan.launch.cwd, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.launch.paths, QStringList({QStringLiteral("/same-profile/path")}));
    QCOMPARE(plan.launch.inNewWindow, true);
    QCOMPARE(plan.launch.reopenLastTabs, false);
}

void ApplicationStartupPlanTest::singleInstanceForwardingMatrixDifferentProfile() {
    Oneg4FM::Startup::CliIntent intent;
    intent.profileName = QStringLiteral("different-profile");
    intent.newWindow = true;
    intent.positionalPaths = {QStringLiteral("/different-profile/path")};

    const auto plan = Oneg4FM::Startup::planForSecondaryInstance(intent, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.keepRunning, false);
    QVERIFY(plan.action == Oneg4FM::Startup::PlannedAction::LaunchFiles);
    QCOMPARE(plan.launch.cwd, QStringLiteral("/tmp/cwd"));
    QCOMPARE(plan.launch.paths, QStringList({QStringLiteral("/different-profile/path")}));
    QCOMPARE(plan.launch.inNewWindow, true);
    QCOMPARE(plan.launch.reopenLastTabs, false);
}

QTEST_MAIN(ApplicationStartupPlanTest)

#include "application_startup_plan_test.moc"
