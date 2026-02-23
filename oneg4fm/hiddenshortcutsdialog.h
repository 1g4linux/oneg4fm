/*
 * Dialog for showing hidden shortcuts
 * oneg4fm/hiddenshortcutsdialog.h
 */

#ifndef PCMANFM_HIDDENSHORTCUTSDIALOG_H
#define PCMANFM_HIDDENSHORTCUTSDIALOG_H

#include <QDialog>

namespace Oneg4FM {

class HiddenShortcutsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit HiddenShortcutsDialog(QWidget* parent = nullptr);
    ~HiddenShortcutsDialog();
};

}  // namespace Oneg4FM

#endif  // PCMANFM_HIDDENSHORTCUTSDIALOG_H
