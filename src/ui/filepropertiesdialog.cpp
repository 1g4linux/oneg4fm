/*
 * Qt-based file properties dialog for oneg4fm
 * oneg4fm/src/ui/filepropertiesdialog.cpp
 */

#include "filepropertiesdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <cmath>
#include "fsqt.h"
#include "visual_style.h"

namespace {

QString formatSize(qint64 bytes) {
    if (bytes < 0) {
        return QStringLiteral("0 B");
    }

    static const char* suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double count = static_cast<double>(bytes);
    int i = 0;

    while (count >= 1024.0 && i < 5) {
        count /= 1024.0;
        ++i;
    }

    return QString::number(count, 'f', (i == 0 ? 0 : 1)) + QLatin1Char(' ') + QLatin1String(suffixes[i]);
}

}  // namespace

FilePropertiesDialog::FilePropertiesDialog(const QList<std::shared_ptr<IFileInfo>>& fileInfos, QWidget* parent)
    : QDialog(parent),
      m_fileInfos(fileInfos),
      m_tabWidget(nullptr),
      m_iconLabel(nullptr),
      m_nameLabel(nullptr),
      m_typeLabel(nullptr),
      m_sizeLabel(nullptr),
      m_locationLabel(nullptr),
      m_modifiedLabel(nullptr),
      m_applyButton(nullptr),
      m_cancelButton(nullptr),
      m_ownerEdit(nullptr),
      m_groupEdit(nullptr),
      m_ownerRead(nullptr),
      m_ownerWrite(nullptr),
      m_ownerExec(nullptr),
      m_groupRead(nullptr),
      m_groupWrite(nullptr),
      m_groupExec(nullptr),
      m_otherRead(nullptr),
      m_otherWrite(nullptr),
      m_otherExec(nullptr),
      m_recursiveCheck(nullptr) {
    setupUI();
    populateFileInfo();
}

FilePropertiesDialog::FilePropertiesDialog(std::shared_ptr<IFileInfo> fileInfo, QWidget* parent)
    : QDialog(parent),
      m_tabWidget(nullptr),
      m_iconLabel(nullptr),
      m_nameLabel(nullptr),
      m_typeLabel(nullptr),
      m_sizeLabel(nullptr),
      m_locationLabel(nullptr),
      m_modifiedLabel(nullptr),
      m_applyButton(nullptr),
      m_cancelButton(nullptr),
      m_ownerEdit(nullptr),
      m_groupEdit(nullptr),
      m_ownerRead(nullptr),
      m_ownerWrite(nullptr),
      m_ownerExec(nullptr),
      m_groupRead(nullptr),
      m_groupWrite(nullptr),
      m_groupExec(nullptr),
      m_otherRead(nullptr),
      m_otherWrite(nullptr),
      m_otherExec(nullptr),
      m_recursiveCheck(nullptr) {
    m_fileInfos.append(std::move(fileInfo));
    setupUI();
    populateFileInfo();
}

void FilePropertiesDialog::setupUI() {
    setWindowTitle(tr("Properties"));
    setMinimumSize(480, 360);

    auto* mainLayout = new QVBoxLayout(this);
    Oneg4FM::UiStyle::applyDialogLayout(mainLayout);

    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(Oneg4FM::UiStyle::kSpacingMd);
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(64, 64);
    topLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

    auto* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(Oneg4FM::UiStyle::kSpacingXs);
    m_nameLabel = new QLabel(this);
    Oneg4FM::UiStyle::applySectionTitle(m_nameLabel);
    infoLayout->addWidget(m_nameLabel);

    m_typeLabel = new QLabel(this);
    infoLayout->addWidget(m_typeLabel);
    topLayout->addLayout(infoLayout);

    mainLayout->addLayout(topLayout);

    m_tabWidget = new QTabWidget(this);
    setupGeneralTab();
    setupPermissionsTab();
    mainLayout->addWidget(m_tabWidget);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    m_applyButton = buttonBox->button(QDialogButtonBox::Apply);
    m_cancelButton = buttonBox->button(QDialogButtonBox::Cancel);

    connect(m_applyButton, &QPushButton::clicked, this, [this]() { onApplyClicked(); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { onCancelClicked(); });

    mainLayout->addWidget(buttonBox);
}

void FilePropertiesDialog::populateFileInfo() {
    if (m_fileInfos.isEmpty()) {
        return;
    }

    const auto& fileInfo = m_fileInfos.first();

    QPixmap iconPixmap = fileInfo->icon().pixmap(64, 64);
    m_iconLabel->setPixmap(iconPixmap);

    if (m_fileInfos.size() == 1) {
        m_nameLabel->setText(fileInfo->displayName());
        m_typeLabel->setText(fileInfo->mimeType());
    }
    else {
        m_nameLabel->setText(tr("%1 items").arg(m_fileInfos.size()));
        m_typeLabel->setText(tr("Multiple items"));
    }
}

void FilePropertiesDialog::setupGeneralTab() {
    auto* generalTab = new QWidget(this);
    auto* layout = new QFormLayout(generalTab);
    layout->setHorizontalSpacing(Oneg4FM::UiStyle::kSpacingSm);
    layout->setVerticalSpacing(Oneg4FM::UiStyle::kSpacingSm);
    layout->setContentsMargins(Oneg4FM::UiStyle::kDialogMargin, Oneg4FM::UiStyle::kDialogMargin,
                               Oneg4FM::UiStyle::kDialogMargin, Oneg4FM::UiStyle::kDialogMargin);

    m_sizeLabel = new QLabel(generalTab);
    m_locationLabel = new QLabel(generalTab);
    m_modifiedLabel = new QLabel(generalTab);

    layout->addRow(tr("Size:"), m_sizeLabel);
    layout->addRow(tr("Location:"), m_locationLabel);
    layout->addRow(tr("Modified:"), m_modifiedLabel);

    if (!m_fileInfos.isEmpty()) {
        if (m_fileInfos.size() == 1) {
            const auto& info = m_fileInfos.first();
            const qint64 size = info->size();
            QFileInfo fi(info->path());

            m_sizeLabel->setText(QStringLiteral("%1 (%2)").arg(size).arg(formatSize(size)));
            m_locationLabel->setText(fi.absolutePath());
            m_modifiedLabel->setText(info->lastModified().toString(Qt::TextDate));
        }
        else {
            qint64 totalSize = 0;
            for (const auto& info : m_fileInfos) {
                totalSize += info->size();
            }
            m_sizeLabel->setText(QStringLiteral("%1 (%2)").arg(totalSize).arg(formatSize(totalSize)));
            m_locationLabel->setText(tr("Multiple locations"));
            m_modifiedLabel->setText(tr("Various"));
        }
    }

    m_tabWidget->addTab(generalTab, tr("General"));
}

void FilePropertiesDialog::setupPermissionsTab() {
    auto* permissionsTab = new QWidget(this);
    auto* layout = new QFormLayout(permissionsTab);
    layout->setHorizontalSpacing(Oneg4FM::UiStyle::kSpacingSm);
    layout->setVerticalSpacing(Oneg4FM::UiStyle::kSpacingSm);
    layout->setContentsMargins(Oneg4FM::UiStyle::kDialogMargin, Oneg4FM::UiStyle::kDialogMargin,
                               Oneg4FM::UiStyle::kDialogMargin, Oneg4FM::UiStyle::kDialogMargin);

    m_ownerEdit = new QLineEdit(permissionsTab);
    m_ownerEdit->setReadOnly(true);

    m_groupEdit = new QLineEdit(permissionsTab);
    m_groupEdit->setReadOnly(true);

    if (!m_fileInfos.isEmpty() && m_fileInfos.size() == 1) {
        QFileInfo fi(m_fileInfos.first()->path());
        m_ownerEdit->setText(fi.owner());
        m_groupEdit->setText(fi.group());
    }
    else if (!m_fileInfos.isEmpty()) {
        m_ownerEdit->setText(tr("Multiple values"));
        m_groupEdit->setText(tr("Multiple values"));
    }

    layout->addRow(tr("Owner:"), m_ownerEdit);
    layout->addRow(tr("Group:"), m_groupEdit);

    auto* permWidget = new QWidget(permissionsTab);
    auto* permLayout = new QGridLayout(permWidget);
    permLayout->setHorizontalSpacing(Oneg4FM::UiStyle::kSpacingSm);
    permLayout->setVerticalSpacing(Oneg4FM::UiStyle::kSpacingXs);

    permLayout->setColumnStretch(0, 0);
    permLayout->setColumnStretch(1, 1);
    permLayout->setColumnStretch(2, 1);
    permLayout->setColumnStretch(3, 1);

    permLayout->addWidget(new QLabel(QString(), permWidget), 0, 0);
    permLayout->addWidget(new QLabel(tr("Read"), permWidget), 0, 1);
    permLayout->addWidget(new QLabel(tr("Write"), permWidget), 0, 2);
    permLayout->addWidget(new QLabel(tr("Execute"), permWidget), 0, 3);

    m_ownerRead = new QCheckBox(permWidget);
    m_ownerWrite = new QCheckBox(permWidget);
    m_ownerExec = new QCheckBox(permWidget);

    m_groupRead = new QCheckBox(permWidget);
    m_groupWrite = new QCheckBox(permWidget);
    m_groupExec = new QCheckBox(permWidget);

    m_otherRead = new QCheckBox(permWidget);
    m_otherWrite = new QCheckBox(permWidget);
    m_otherExec = new QCheckBox(permWidget);

    permLayout->addWidget(new QLabel(tr("Owner"), permWidget), 1, 0);
    permLayout->addWidget(m_ownerRead, 1, 1);
    permLayout->addWidget(m_ownerWrite, 1, 2);
    permLayout->addWidget(m_ownerExec, 1, 3);

    permLayout->addWidget(new QLabel(tr("Group"), permWidget), 2, 0);
    permLayout->addWidget(m_groupRead, 2, 1);
    permLayout->addWidget(m_groupWrite, 2, 2);
    permLayout->addWidget(m_groupExec, 2, 3);

    permLayout->addWidget(new QLabel(tr("Others"), permWidget), 3, 0);
    permLayout->addWidget(m_otherRead, 3, 1);
    permLayout->addWidget(m_otherWrite, 3, 2);
    permLayout->addWidget(m_otherExec, 3, 3);

    if (!m_fileInfos.isEmpty() && m_fileInfos.size() == 1) {
        QFileInfo fi(m_fileInfos.first()->path());
        const QFile::Permissions perms = fi.permissions();

        m_ownerRead->setCheckState(perms.testFlag(QFile::ReadOwner) ? Qt::Checked : Qt::Unchecked);
        m_ownerWrite->setCheckState(perms.testFlag(QFile::WriteOwner) ? Qt::Checked : Qt::Unchecked);
        m_ownerExec->setCheckState(perms.testFlag(QFile::ExeOwner) ? Qt::Checked : Qt::Unchecked);

        m_groupRead->setCheckState(perms.testFlag(QFile::ReadGroup) ? Qt::Checked : Qt::Unchecked);
        m_groupWrite->setCheckState(perms.testFlag(QFile::WriteGroup) ? Qt::Checked : Qt::Unchecked);
        m_groupExec->setCheckState(perms.testFlag(QFile::ExeGroup) ? Qt::Checked : Qt::Unchecked);

        m_otherRead->setCheckState(perms.testFlag(QFile::ReadOther) ? Qt::Checked : Qt::Unchecked);
        m_otherWrite->setCheckState(perms.testFlag(QFile::WriteOther) ? Qt::Checked : Qt::Unchecked);
        m_otherExec->setCheckState(perms.testFlag(QFile::ExeOther) ? Qt::Checked : Qt::Unchecked);
    }
    else if (!m_fileInfos.isEmpty()) {
        m_ownerRead->setTristate(true);
        m_ownerWrite->setTristate(true);
        m_ownerExec->setTristate(true);
        m_groupRead->setTristate(true);
        m_groupWrite->setTristate(true);
        m_groupExec->setTristate(true);
        m_otherRead->setTristate(true);
        m_otherWrite->setTristate(true);
        m_otherExec->setTristate(true);

        m_ownerRead->setCheckState(Qt::PartiallyChecked);
        m_ownerWrite->setCheckState(Qt::PartiallyChecked);
        m_ownerExec->setCheckState(Qt::PartiallyChecked);
        m_groupRead->setCheckState(Qt::PartiallyChecked);
        m_groupWrite->setCheckState(Qt::PartiallyChecked);
        m_groupExec->setCheckState(Qt::PartiallyChecked);
        m_otherRead->setCheckState(Qt::PartiallyChecked);
        m_otherWrite->setCheckState(Qt::PartiallyChecked);
        m_otherExec->setCheckState(Qt::PartiallyChecked);
    }

    layout->addRow(tr("Permissions:"), permWidget);

    m_recursiveCheck = new QCheckBox(tr("Apply changes recursively to subfolders and files"), permissionsTab);
    layout->addRow(QString(), m_recursiveCheck);

    m_tabWidget->addTab(permissionsTab, tr("Permissions"));
}

void FilePropertiesDialog::onApplyClicked() {
    if (m_fileInfos.isEmpty()) {
        accept();
        return;
    }

    auto adjustBit = [](QFile::Permissions& perms, QFile::Permission bit, Qt::CheckState state) {
        if (state == Qt::PartiallyChecked) {
            return;
        }
        if (state == Qt::Checked) {
            perms |= bit;
        }
        else {
            perms &= ~bit;
        }
    };

    const Qt::CheckState ownerReadState = m_ownerRead->checkState();
    const Qt::CheckState ownerWriteState = m_ownerWrite->checkState();
    const Qt::CheckState ownerExecState = m_ownerExec->checkState();

    const Qt::CheckState groupReadState = m_groupRead->checkState();
    const Qt::CheckState groupWriteState = m_groupWrite->checkState();
    const Qt::CheckState groupExecState = m_groupExec->checkState();

    const Qt::CheckState otherReadState = m_otherRead->checkState();
    const Qt::CheckState otherWriteState = m_otherWrite->checkState();
    const Qt::CheckState otherExecState = m_otherExec->checkState();

    const bool recursive = m_recursiveCheck->isChecked();

    auto applyToPath = [&](const QString& path, QString& error) -> bool {
        QFileInfo fi(path);
        QFile::Permissions perms = fi.permissions();

        adjustBit(perms, QFile::ReadOwner, ownerReadState);
        adjustBit(perms, QFile::WriteOwner, ownerWriteState);
        adjustBit(perms, QFile::ExeOwner, ownerExecState);

        adjustBit(perms, QFile::ReadGroup, groupReadState);
        adjustBit(perms, QFile::WriteGroup, groupWriteState);
        adjustBit(perms, QFile::ExeGroup, groupExecState);

        adjustBit(perms, QFile::ReadOther, otherReadState);
        adjustBit(perms, QFile::WriteOther, otherWriteState);
        adjustBit(perms, QFile::ExeOther, otherExecState);

        if (!Oneg4FM::FsQt::setPermissions(path, perms, error)) {
            error = tr("Failed to change permissions for %1: %2").arg(path, error);
            return false;
        }

        return true;
    };

    QString error;
    for (const auto& info : m_fileInfos) {
        const QString rootPath = info->path();
        if (rootPath.isEmpty()) {
            continue;
        }

        if (!applyToPath(rootPath, error)) {
            break;
        }

        if (recursive) {
            QFileInfo fi(rootPath);
            if (fi.isDir()) {
                QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
                QDirIterator it(rootPath, filters, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString subPath = it.next();
                    if (!applyToPath(subPath, error)) {
                        break;
                    }
                }
            }
        }

        if (!error.isEmpty()) {
            break;
        }
    }

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Permissions"), error);
        return;
    }

    accept();
}

void FilePropertiesDialog::onCancelClicked() {
    reject();
}
