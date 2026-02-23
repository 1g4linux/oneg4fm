/*
 * Dialog for creating desktop launcher files
 * oneg4fm/createlauncherdialog.h
 */

#ifndef PCMANFM_CREATELAUNCHERDIALOG_H
#define PCMANFM_CREATELAUNCHERDIALOG_H

#include <QDialog>
#include <QLineEdit>

namespace Ui {
class CreateLauncherDialog;
}

namespace Oneg4FM {

class CreateLauncherDialog : public QDialog {
    Q_OBJECT

   public:
    explicit CreateLauncherDialog(QWidget* parent = nullptr);
    ~CreateLauncherDialog();

    QString launcherName() const;
    QString launcherCommand() const;

   private:
    Ui::CreateLauncherDialog* ui;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_CREATELAUNCHERDIALOG_H
