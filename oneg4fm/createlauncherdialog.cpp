/*
 * Dialog for creating desktop launcher files
 * oneg4fm/createlauncherdialog.cpp
 */

#include "createlauncherdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>

namespace Oneg4FM {

CreateLauncherDialog::CreateLauncherDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Create Launcher"));

    QFormLayout* formLayout = new QFormLayout(this);

    QLabel* nameLabel = new QLabel(tr("Name:"), this);
    QLineEdit* nameEdit = new QLineEdit(this);
    formLayout->addRow(nameLabel, nameEdit);
    connect(this, &CreateLauncherDialog::accepted, [nameEdit, this]() {
        // Store nameEdit->text() temporarily for launcherName() access
        setProperty("launcherName", nameEdit->text());
    });

    QLabel* commandLabel = new QLabel(tr("Command:"), this);
    QLineEdit* commandEdit = new QLineEdit(this);
    formLayout->addRow(commandLabel, commandEdit);
    connect(this, &CreateLauncherDialog::accepted, [commandEdit, this]() {
        // Store commandEdit->text() temporarily for launcherCommand() access
        setProperty("launcherCommand", commandEdit->text());
    });

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    formLayout->addRow(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &CreateLauncherDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &CreateLauncherDialog::reject);
}

CreateLauncherDialog::~CreateLauncherDialog() {
    // ui is not used; it's a direct layout here.
    // If a .ui file was used, it would be 'delete ui;'
}

QString CreateLauncherDialog::launcherName() const {
    return property("launcherName").toString();
}

QString CreateLauncherDialog::launcherCommand() const {
    return property("launcherCommand").toString();
}

}  // namespace Oneg4FM
