/*
 * tests/mainwindow_tab_commands_test.cpp
 */

#include <QList>
#include <QTest>

#include "../oneg4fm/mainwindow_tab_commands.h"

namespace {

class FakeTabContext final : public Oneg4FM::MainWindowTabCommands::Context {
   public:
    int currentIndexValue = -1;
    int tabCountValue = 0;
    int focusCalls = 0;
    QList<int> setCurrentIndexCalls;
    QList<int> closeTabCalls;

    int currentTabIndex() const override { return currentIndexValue; }

    int tabCount() const override { return tabCountValue; }

    void setCurrentTabIndex(int index) override {
        setCurrentIndexCalls.append(index);
        currentIndexValue = index;
    }

    void closeTabAt(int index) override {
        closeTabCalls.append(index);
        if (index < 0 || index >= tabCountValue) {
            return;
        }

        --tabCountValue;
        if (tabCountValue == 0) {
            currentIndexValue = -1;
            return;
        }

        if (currentIndexValue > index) {
            --currentIndexValue;
        }
        if (currentIndexValue >= tabCountValue) {
            currentIndexValue = tabCountValue - 1;
        }
    }

    void focusCurrentTabView() override { ++focusCalls; }
};

}  // namespace

class MainWindowTabCommandsTest : public QObject {
    Q_OBJECT

   private Q_SLOTS:
    void previousTabWrapsAndGuards();
    void nextTabWrapsAndGuards();
    void closeLeftTabsClosesInDescendingOrder();
    void closeRightTabsClosesInDescendingOrder();
    void closeOtherTabsClosesBothSides();
    void focusCurrentTabViewRequiresActiveTab();
    void scopedContextOverridesActiveContext();
    void scopedContextFallbackUsesActiveContext();
};

void MainWindowTabCommandsTest::previousTabWrapsAndGuards() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext noTabs;
    QVERIFY(!canExecute(Id::PreviousTab, noTabs));
    execute(Id::PreviousTab, noTabs);
    QCOMPARE(noTabs.setCurrentIndexCalls.size(), 0);

    FakeTabContext wrapped;
    wrapped.currentIndexValue = 0;
    wrapped.tabCountValue = 3;
    QVERIFY(canExecute(Id::PreviousTab, wrapped));
    execute(Id::PreviousTab, wrapped);
    QCOMPARE(wrapped.setCurrentIndexCalls, QList<int>({2}));

    FakeTabContext moved;
    moved.currentIndexValue = 2;
    moved.tabCountValue = 3;
    execute(Id::PreviousTab, moved);
    QCOMPARE(moved.setCurrentIndexCalls, QList<int>({1}));
}

void MainWindowTabCommandsTest::nextTabWrapsAndGuards() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext noTabs;
    QVERIFY(!canExecute(Id::NextTab, noTabs));
    execute(Id::NextTab, noTabs);
    QCOMPARE(noTabs.setCurrentIndexCalls.size(), 0);

    FakeTabContext wrapped;
    wrapped.currentIndexValue = 2;
    wrapped.tabCountValue = 3;
    QVERIFY(canExecute(Id::NextTab, wrapped));
    execute(Id::NextTab, wrapped);
    QCOMPARE(wrapped.setCurrentIndexCalls, QList<int>({0}));

    FakeTabContext moved;
    moved.currentIndexValue = 0;
    moved.tabCountValue = 3;
    execute(Id::NextTab, moved);
    QCOMPARE(moved.setCurrentIndexCalls, QList<int>({1}));
}

void MainWindowTabCommandsTest::closeLeftTabsClosesInDescendingOrder() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext context;
    context.currentIndexValue = 3;
    context.tabCountValue = 5;
    QVERIFY(canExecute(Id::CloseLeftTabs, context));
    execute(Id::CloseLeftTabs, context);
    QCOMPARE(context.closeTabCalls, QList<int>({2, 1, 0}));
}

void MainWindowTabCommandsTest::closeRightTabsClosesInDescendingOrder() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext context;
    context.currentIndexValue = 1;
    context.tabCountValue = 5;
    QVERIFY(canExecute(Id::CloseRightTabs, context));
    execute(Id::CloseRightTabs, context);
    QCOMPARE(context.closeTabCalls, QList<int>({4, 3, 2}));
}

void MainWindowTabCommandsTest::closeOtherTabsClosesBothSides() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext context;
    context.currentIndexValue = 2;
    context.tabCountValue = 5;
    QVERIFY(canExecute(Id::CloseOtherTabs, context));
    execute(Id::CloseOtherTabs, context);
    QCOMPARE(context.closeTabCalls, QList<int>({4, 3, 1, 0}));
}

void MainWindowTabCommandsTest::focusCurrentTabViewRequiresActiveTab() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext noTabs;
    QVERIFY(!canExecute(Id::FocusCurrentTabView, noTabs));
    execute(Id::FocusCurrentTabView, noTabs);
    QCOMPARE(noTabs.focusCalls, 0);

    FakeTabContext context;
    context.currentIndexValue = 0;
    context.tabCountValue = 1;
    QVERIFY(canExecute(Id::FocusCurrentTabView, context));
    execute(Id::FocusCurrentTabView, context);
    QCOMPARE(context.focusCalls, 1);
}

void MainWindowTabCommandsTest::scopedContextOverridesActiveContext() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext activeContext;
    activeContext.currentIndexValue = 1;
    activeContext.tabCountValue = 3;

    FakeTabContext scopedContext;
    scopedContext.currentIndexValue = 3;
    scopedContext.tabCountValue = 5;

    QVERIFY(canExecute(Id::CloseLeftTabs, activeContext, &scopedContext));
    execute(Id::CloseLeftTabs, activeContext, &scopedContext);

    QCOMPARE(scopedContext.closeTabCalls, QList<int>({2, 1, 0}));
    QCOMPARE(activeContext.closeTabCalls.size(), 0);
}

void MainWindowTabCommandsTest::scopedContextFallbackUsesActiveContext() {
    using namespace Oneg4FM::MainWindowTabCommands;

    FakeTabContext activeContext;
    activeContext.currentIndexValue = 2;
    activeContext.tabCountValue = 4;

    QVERIFY(canExecute(Id::CloseRightTabs, activeContext, nullptr));
    execute(Id::CloseRightTabs, activeContext, nullptr);

    QCOMPARE(activeContext.closeTabCalls, QList<int>({3}));
}

QTEST_MAIN(MainWindowTabCommandsTest)

#include "mainwindow_tab_commands_test.moc"
