/*
 * Dialog for showing hidden shortcuts
 * oneg4fm/hiddenshortcutsdialog.cpp
 */

#include "hiddenshortcutsdialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>

namespace Oneg4FM {

HiddenShortcutsDialog::HiddenShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Hidden Shortcuts"));
    resize(400, 300);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* label = new QLabel(this);
    label->setText(
        tr("<b>General:</b><br>"
           "F3: Find files<br>"
           "F4: Open terminal<br>"
           "F9: Toggle side pane<br>"
           "Ctrl+M: Toggle menu bar<br>"
           "Ctrl+W: Close tab<br>"
           "Ctrl+Q: Close window<br>"
           "<br>"
           "<b>Navigation:</b><br>"
           "Alt+Up: Go up<br>"
           "Alt+Left: Go back<br>"
           "Alt+Right: Go forward<br>"
           "Backspace: Go up (optional)<br>"));
    layout->addWidget(label);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &HiddenShortcutsDialog::reject);
}

HiddenShortcutsDialog::~HiddenShortcutsDialog() {}

}  // namespace Oneg4FM
