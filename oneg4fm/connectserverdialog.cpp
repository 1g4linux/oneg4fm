/*
 * Connect to server dialog implementation
 * oneg4fm/connectserverdialog.cpp
 */

#include "connectserverdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace Oneg4FM {

ConnectServerDialog::ConnectServerDialog(QWidget* parent)
    : QDialog(parent),
      serverTypes{
          {tr("SSH"), "sftp", 22, false},  {tr("FTP"), "ftp", 21, true},
          {tr("WebDav"), "dav", 80, true}, {tr("Secure WebDav"), "davs", 443, false},
          {tr("HTTP"), "http", 80, true},  {tr("HTTPS"), "https", 443, true},
      } {
    ui.setupUi(this);

    for (const auto& serverType : serverTypes) {
        ui.serverType->addItem(serverType.name);
    }

    connect(ui.serverType, &QComboBox::currentIndexChanged, this, &ConnectServerDialog::onCurrentIndexChanged);

    connect(ui.host, &QLineEdit::textChanged, this, &ConnectServerDialog::checkInput);
    connect(ui.userName, &QLineEdit::textChanged, this, &ConnectServerDialog::checkInput);

    ui.serverType->setCurrentIndex(0);
    onCurrentIndexChanged(0);
}

ConnectServerDialog::~ConnectServerDialog() = default;

QString ConnectServerDialog::uriText() {
    const int serverTypeIdx = ui.serverType->currentIndex();
    if (serverTypeIdx < 0 || serverTypeIdx >= serverTypes.size()) {
        return {};
    }

    const auto& serverType = serverTypes[serverTypeIdx];

    QString uri = QString::fromLatin1(serverType.scheme);
    uri += QStringLiteral("://");

    if (ui.loginAsUser->isChecked()) {
        const QString user = ui.userName->text().trimmed();
        if (!user.isEmpty()) {
            uri += user;
            uri += QLatin1Char('@');
        }
    }

    uri += ui.host->text().trimmed();

    const int port = ui.port->value();
    if (port != serverType.defaultPort) {
        uri += QLatin1Char(':');
        uri += QString::number(port);
    }

    QString path = ui.path->text();
    if (path.isEmpty() || path.at(0) != QLatin1Char('/')) {
        uri += QLatin1Char('/');
    }
    uri += path;

    return uri;
}

void ConnectServerDialog::onCurrentIndexChanged(int index) {
    int serverTypeIdx = index;
    if (serverTypeIdx < 0 || serverTypeIdx >= serverTypes.size()) {
        serverTypeIdx = ui.serverType->currentIndex();
    }
    if (serverTypeIdx < 0 || serverTypeIdx >= serverTypes.size()) {
        checkInput();
        return;
    }

    const auto& serverType = serverTypes[serverTypeIdx];

    ui.port->setValue(serverType.defaultPort);
    ui.anonymousLogin->setEnabled(serverType.canAnonymous);

    if (serverType.canAnonymous) {
        ui.anonymousLogin->setChecked(true);
    }
    else {
        ui.loginAsUser->setChecked(true);
    }

    ui.host->setFocus();
    checkInput();
}

void ConnectServerDialog::checkInput() {
    bool valid = true;

    const QString hostText = ui.host->text().trimmed();
    const QString userText = ui.userName->text().trimmed();

    if (hostText.isEmpty()) {
        valid = false;
    }
    else if (ui.loginAsUser->isChecked() && userText.isEmpty()) {
        valid = false;
    }

    if (auto* okButton = ui.buttonBox->button(QDialogButtonBox::Ok)) {
        okButton->setEnabled(valid);
    }
}

}  // namespace Oneg4FM
