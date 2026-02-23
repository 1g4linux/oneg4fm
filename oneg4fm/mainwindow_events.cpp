/*
 * Main window event handling implementation
 * oneg4fm/mainwindow_events.cpp
 */

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"  // Required for casting widgets to TabPage

// Qt Headers
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextEdit>

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

// Helper to calculate the "dimmed" palette for inactive view frames.
// This visually distinguishes the active split pane from the inactive one.
QPalette getInactivePalette(const QPalette& sourcePalette) {
    QPalette palette = sourcePalette;

    QColor txtCol = palette.color(QPalette::Text);
    QColor baseCol = palette.color(QPalette::Base);

    // Mix 90% base color with 10% text color to slightly darken/tint the background
    baseCol.setRgbF(0.9 * baseCol.redF() + 0.1 * txtCol.redF(), 0.9 * baseCol.greenF() + 0.1 * txtCol.greenF(),
                    0.9 * baseCol.blueF() + 0.1 * txtCol.blueF(), baseCol.alphaF());

    palette.setColor(QPalette::Base, baseCol);

    // Dim the text colors
    constexpr qreal kAlphaFactor = 0.7;

    txtCol.setAlphaF(txtCol.alphaF() * kAlphaFactor);
    palette.setColor(QPalette::Text, txtCol);

    QColor winTxt = palette.color(QPalette::WindowText);
    winTxt.setAlphaF(winTxt.alphaF() * kAlphaFactor);
    palette.setColor(QPalette::WindowText, winTxt);

    QColor btnTxt = palette.color(QPalette::ButtonText);
    btnTxt.setAlphaF(btnTxt.alphaF() * kAlphaFactor);
    palette.setColor(QPalette::Active, QPalette::ButtonText, btnTxt);
    palette.setColor(QPalette::Inactive, QPalette::ButtonText, btnTxt);

    return palette;
}

}  // namespace

void MainWindow::applyFrameActivation(ViewFrame* newActiveFrame) {
    if (newActiveFrame) {
        activeViewFrame_ = newActiveFrame;
    }

    for (int i = 0; i < ui.viewSplitter->count(); ++i) {
        auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));
        if (!viewFrame) {
            continue;
        }

        const bool isActive = (viewFrame == activeViewFrame_);
        viewFrame->setPalette(isActive ? qApp->palette() : getInactivePalette(viewFrame->palette()));

        if (!isActive) {
            if (auto* sw = viewFrame->getStackedWidget()) {
                for (int idx = 0; idx < sw->count(); ++idx) {
                    if (auto* page = qobject_cast<TabPage*>(sw->widget(idx))) {
                        page->deselectAll();
                    }
                }
            }
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    if (!watchedWidget) {
        return QMainWindow::eventFilter(watched, event);
    }

    // Only filter events happening inside the view splitter
    if (!ui.viewSplitter->isAncestorOf(watchedWidget)) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::FocusIn) {
        handleFocusIn(watchedWidget);
        // Fall through to base class processing is intended for FocusIn
    }
    else if (event->type() == QEvent::KeyPress) {
        if (handleTabKey(static_cast<QKeyEvent*>(event), watchedWidget)) {
            return true;  // Event consumed
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::handleFocusIn(QWidget* watchedWidget) {
    for (int i = 0; i < ui.viewSplitter->count(); ++i) {
        auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));
        if (!viewFrame)
            continue;

        if (viewFrame->isAncestorOf(watchedWidget)) {
            // This is the active frame
            if (activeViewFrame_ != viewFrame) {
                updateUIForCurrentPage(false);  // WARNING: never set focus here to avoid recursion!
            }
            applyFrameActivation(viewFrame);
        }
    }
}

bool MainWindow::handleTabKey(QKeyEvent* ke, QWidget* watchedWidget) {
    // Only handle Tab (without modifiers) for switching panes
    if (ke->key() != Qt::Key_Tab || ke->modifiers() != Qt::NoModifier) {
        return false;
    }

    // Do not intercept Tab if editing text (e.g., renaming a file)
    if (qobject_cast<QTextEdit*>(watchedWidget)) {
        return false;
    }

    // Cycle focus to the next view frame
    for (int i = 0; i < ui.viewSplitter->count(); ++i) {
        auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));

        if (viewFrame && activeViewFrame_ == viewFrame) {
            // Find next index, wrapping around
            const int nextIndex = (i < ui.viewSplitter->count() - 1) ? i + 1 : 0;
            auto* nextFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(nextIndex));

            if (nextFrame) {
                applyFrameActivation(nextFrame);
                updateUIForCurrentPage();  // This sets focus to the view inside the frame
                return true;
            }
        }
    }

    return false;
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);

    Settings& settings = appSettings();
    if (settings.rememberWindowSize()) {
        settings.setLastWindowMaximized(isMaximized());

        if (!isMaximized()) {
            settings.setLastWindowWidth(width());
            settings.setLastWindowHeight(height());
            settings.setLastWindowX(x());
            settings.setLastWindowY(y());
        }
    }
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);

    Settings& settings = appSettings();
    if (settings.rememberWindowSize()) {
        if (!isMaximized()) {
            settings.setLastWindowX(x());
            settings.setLastWindowY(y());
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (lastActive_ == this) {
        lastActive_ = nullptr;
    }

    Settings& settings = appSettings();

    // Save window geometry
    if (settings.rememberWindowSize()) {
        settings.setLastWindowMaximized(isMaximized());

        if (!isMaximized()) {
            settings.setLastWindowWidth(width());
            settings.setLastWindowHeight(height());
            settings.setLastWindowX(x());
            settings.setLastWindowY(y());
        }
    }

    // Save tab paths (only if this is the last active window)
    // This ensures that when the user re-opens the app, they get the state of the last closed window.
    if (lastActive_ == nullptr && settings.reopenLastTabs()) {
        QStringList tabPaths;
        int splitNum = 0;

        for (int i = 0; i < ui.viewSplitter->count(); ++i) {
            auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));
            if (!viewFrame)
                continue;

            auto* stack = viewFrame->getStackedWidget();
            const int n = stack->count();

            for (int j = 0; j < n; ++j) {
                if (auto* page = qobject_cast<TabPage*>(stack->widget(j))) {
                    // LibFM-Qt path conversion
                    tabPaths.append(QString::fromUtf8(page->path().toString().get()));
                }
            }

            // Record where the split occurs (after the tabs of the first frame)
            if (i == 0 && ui.viewSplitter->count() > 1) {
                splitNum = tabPaths.size();
            }
        }

        settings.setTabPaths(tabPaths);
        settings.setSplitViewTabsNum(splitNum);
    }

    QMainWindow::closeEvent(event);
}

bool MainWindow::event(QEvent* event) {
    if (event->type() == QEvent::WindowActivate) {
        lastActive_ = this;
    }
    return QMainWindow::event(event);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LayoutDirectionChange) {
        setRTLIcons(QApplication::layoutDirection() == Qt::RightToLeft);
    }
    QMainWindow::changeEvent(event);
}

}  // namespace Oneg4FM
