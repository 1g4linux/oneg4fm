/*
 * Preferences dialog header
 * oneg4fm/preferencesdialog.h
 */

#ifndef PCMANFM_PREFERENCESDIALOG_H
#define PCMANFM_PREFERENCESDIALOG_H

#include <QDialog>
#include <QString>

#include "ui_preferences.h"

namespace Oneg4FM {

class Settings;

class PreferencesDialog : public QDialog {
    Q_OBJECT

   public:
    // activePage is the name of page to select (general, display, advanced...)
    explicit PreferencesDialog(const QString& activePage = QString(), QWidget* parent = nullptr);
    virtual ~PreferencesDialog();

    virtual void accept();

    void selectPage(const QString& name);

   protected Q_SLOTS:
    void lockMargins(bool lock);
    void terminalChanged(int index);
    void terminalContextMenu(const QPoint& p);
    void applySettings();

   private:
    void initIconThemes(Settings& settings);
    void initArchivers(Settings& settings);
    void initDisplayPage(Settings& settings);
    void initUiPage(Settings& settings);
    void initBehaviorPage(Settings& settings);
    void initThumbnailPage(Settings& settings);
    void initVolumePage(Settings& settings);
    void initAdvancedPage(Settings& settings);
    void initTerminals(Settings& settings);

    void applyTerminal(Settings& settings);

    void applyUiPage(Settings& settings);
    void applyDisplayPage(Settings& settings);
    void applyBehaviorPage(Settings& settings);
    void applyThumbnailPage(Settings& settings);
    void applyVolumePage(Settings& settings);
    void applyAdvancedPage(Settings& settings);

    void initFromSettings();

    void restartWarning(bool warn);

   private:
    Ui::PreferencesDialog ui;
    int warningCounter_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_PREFERENCESDIALOG_H
