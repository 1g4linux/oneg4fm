/*
 * Bulk file renaming implementation for oneg4fm
 * oneg4fm/bulkrename.cpp
 */

#include "bulkrename.h"

// If you are migrating away from libfm-qt entirely, you will eventually
// replace this header with your own Core/FileOps interface.
#include "panel/panel.h"

#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

namespace Oneg4FM {

namespace {

// Helper to resolve a usable name from FileInfo, respecting edit name and encoding
// Refactored to avoid direct GLib calls where possible if the wrapper allows,
// otherwise ensuring safe string conversion.
QString effectiveFileName(const std::shared_ptr<const Panel::FileInfo>& file) {
    // If migrating away from libfm, this would use QFileInfo directly.
    // For now, we prefer the display name logic but handle potential empty names.

    // Original GLib fallback logic in pure Qt:
    QString name = QString::fromStdString(file->name());

    // If the file info has a specific "edit name" (different from display name), use it.
    // Assuming file->displayName() is the intended starting point for renames
    // unless the raw name is strictly required for filesystem operations.
    if (name.isEmpty()) {
        name = file->displayName();
    }

    return name;
}

}  // namespace

BulkRenameDialog::BulkRenameDialog(QWidget* parent, Qt::WindowFlags flags) : QDialog(parent, flags) {
    ui.setupUi(this);
    ui.lineEdit->setFocus();

    connect(ui.buttonBox->button(QDialogButtonBox::Ok), &QAbstractButton::clicked, this, &QDialog::accept);
    connect(ui.buttonBox->button(QDialogButtonBox::Cancel), &QAbstractButton::clicked, this, &QDialog::reject);

    // Mutual exclusivity logic for group boxes
    auto makeExclusive = [this](QGroupBox* active, QGroupBox* b1, QGroupBox* b2) {
        connect(active, &QGroupBox::clicked, this, [active, b1, b2](bool checked) {
            if (!checked) {
                active->setChecked(true);  // Enforce radio-button-like behavior (at least one active)
            }
            else {
                b1->setChecked(false);
                b2->setChecked(false);
            }
        });
    };

    makeExclusive(ui.serialGroupBox, ui.replaceGroupBox, ui.caseGroupBox);
    makeExclusive(ui.replaceGroupBox, ui.serialGroupBox, ui.caseGroupBox);
    makeExclusive(ui.caseGroupBox, ui.serialGroupBox, ui.replaceGroupBox);

    resize(minimumSize());
    setMaximumHeight(minimumSizeHint().height());
}

void BulkRenameDialog::setState(const QString& baseName,
                                const QString& findStr,
                                const QString& replaceStr,
                                bool replacement,
                                bool caseChange,
                                bool zeroPadding,
                                bool respectLocale,
                                bool regex,
                                bool toUpperCase,
                                int start,
                                Qt::CaseSensitivity cs) {
    if (!baseName.isEmpty()) {
        ui.lineEdit->setText(baseName);
    }

    ui.spinBox->setValue(start);
    ui.zeroBox->setChecked(zeroPadding);
    ui.localeBox->setChecked(respectLocale);

    if (replacement || caseChange) {
        ui.serialGroupBox->setChecked(false);
        if (replacement) {
            ui.replaceGroupBox->setChecked(true);
        }
        else {
            ui.caseGroupBox->setChecked(true);
        }
    }
    else {
        // Default to serial if nothing else is specified
        ui.serialGroupBox->setChecked(true);
    }

    ui.findLineEdit->setText(findStr);
    ui.replaceLineEdit->setText(replaceStr);
    ui.caseBox->setChecked(cs == Qt::CaseSensitive);
    ui.regexBox->setChecked(regex);

    if (toUpperCase) {
        ui.upperCaseButton->setChecked(true);
    }
    else {
        ui.lowerCaseButton->setChecked(true);
    }
}

void BulkRenameDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);

    // If the pattern ends with '#', pre-select the text before it for easy editing
    if (ui.lineEdit->text().endsWith(QLatin1Char('#'))) {
        QTimer::singleShot(0, this, [this]() { ui.lineEdit->setSelection(0, ui.lineEdit->text().size() - 1); });
    }
}

//-----------------------------------------------------------------------------
// BulkRenamer Class
//-----------------------------------------------------------------------------

BulkRenamer::BulkRenamer(const Panel::FileInfoList& files, QWidget* parent) {
    if (files.size() <= 1) {
        return;
    }

    // State variables to persist across dialog re-opens (if error occurs and user retries)
    bool replacement = false;
    bool caseChange = false;
    QString baseName;
    QString findStr;
    QString replaceStr;
    int start = 0;
    bool zeroPadding = false;
    bool respectLocale = false;
    bool regex = false;
    bool toUpperCase = true;
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    QLocale locale;

    bool showDlg = true;

    while (showDlg) {
        BulkRenameDialog dlg(parent);
        dlg.setState(baseName, findStr, replaceStr, replacement, caseChange, zeroPadding, respectLocale, regex,
                     toUpperCase, start, cs);

        if (dlg.exec() != QDialog::Accepted) {
            return;
        }

        // Retrieve state from dialog
        baseName = dlg.getBaseName();
        start = dlg.getStart();
        zeroPadding = dlg.getZeroPadding();
        respectLocale = dlg.getRespectLocale();
        locale = dlg.locale();

        replacement = dlg.getReplace();
        findStr = dlg.getFindStr();
        replaceStr = dlg.getReplaceStr();
        cs = dlg.getCase();
        regex = dlg.getRegex();

        caseChange = dlg.getCaseChange();
        toUpperCase = dlg.getUpperCase();

        // Execute the appropriate rename strategy
        bool success = false;
        if (replacement) {
            success = renameByReplacing(files, findStr, replaceStr, cs, regex, parent);
        }
        else if (caseChange) {
            success = renameByChangingCase(files, locale, toUpperCase, parent);
        }
        else {
            success = rename(files, baseName, locale, start, zeroPadding, respectLocale, parent);
        }

        // If rename failed (e.g., collisions), re-show dialog to let user fix settings
        showDlg = !success;
    }
}

BulkRenamer::~BulkRenamer() = default;

bool BulkRenamer::rename(const Panel::FileInfoList& files,
                         QString& baseName,
                         const QLocale& locale,
                         int start,
                         bool zeroPadding,
                         bool respectLocale,
                         QWidget* parent) {
    // Ensure the pattern has a placeholder for the number
    if (!baseName.contains(QLatin1Char('#'))) {
        int end = baseName.lastIndexOf(QLatin1Char('.'));
        if (end == -1) {
            end = baseName.size();
        }
        baseName.insert(end, QLatin1Char('#'));
    }

    // Determine formatting specifics
    const int numSpace = zeroPadding ? QString::number(start + int(files.size())).size() : 0;
    const QChar zeroDigit = respectLocale
                                ? (!locale.zeroDigit().isEmpty() ? locale.zeroDigit().at(0) : QLatin1Char('0'))
                                : QLatin1Char('0');
    const QString specifier = respectLocale ? QStringLiteral("%L1") : QStringLiteral("%1");

    // Detect if extension preservation is needed
    // Simple logic: if baseName has no extension part in the pattern, try to keep original
    const QRegularExpression extensionRegExp(QStringLiteral("\\.[^.#]+$"));
    const bool preserveExtension = (baseName.indexOf(extensionRegExp) == -1);

    QProgressDialog progress(QObject::tr("Renaming files..."), QObject::tr("Abort"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);

    int i = 0;
    int failed = 0;

    for (const auto& file : files) {
        progress.setValue(i);

        if (progress.wasCanceled()) {
            progress.close();
            QMessageBox::warning(parent, QObject::tr("Warning"), QObject::tr("Renaming is aborted."));
            return true;  // Return true because the user explicitly cancelled, not an error
        }

        QString fileName = effectiveFileName(file);
        QString newName = baseName;

        // Append original extension if required
        if (preserveExtension) {
            QRegularExpressionMatch match;
            if (fileName.indexOf(extensionRegExp, 0, &match) > -1) {
                newName += match.captured();
            }
        }

        // Insert number
        newName.replace(QLatin1Char('#'), specifier.arg(start + i, numSpace, 10, zeroDigit));

        // Skip rename if name hasn't changed
        if (newName != fileName) {
            // NOTE: Panel::changeFileName is a LibFM-Qt function.
            // In the future architecture, this should call BackendRegistry::fileOps()->rename(...)
            if (!Panel::changeFileName(file->path(), newName, nullptr, false)) {
                ++failed;
            }
        }

        ++i;
    }

    progress.setValue(i);

    if (failed == i && i > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("No file could be renamed."));
        return false;
    }

    if (failed > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Some files could not be renamed."));
    }

    return true;
}

bool BulkRenamer::renameByReplacing(const Panel::FileInfoList& files,
                                    const QString& findStr,
                                    const QString& replaceStr,
                                    Qt::CaseSensitivity cs,
                                    bool regex,
                                    QWidget* parent) {
    if (findStr.isEmpty()) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Nothing to find."));
        return false;
    }

    QRegularExpression regexFind;
    if (regex) {
        QRegularExpression::PatternOptions options =
            (cs == Qt::CaseSensitive) ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
        regexFind.setPattern(findStr);
        regexFind.setPatternOptions(options);

        if (!regexFind.isValid()) {
            QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Invalid regular expression."));
            return false;
        }
    }

    QProgressDialog progress(QObject::tr("Renaming files..."), QObject::tr("Abort"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);

    int i = 0;
    int failed = 0;

    for (const auto& file : files) {
        progress.setValue(i);

        if (progress.wasCanceled()) {
            progress.close();
            QMessageBox::warning(parent, QObject::tr("Warning"), QObject::tr("Renaming is aborted."));
            return true;
        }

        QString fileName = effectiveFileName(file);
        QString newName = fileName;

        if (regex) {
            newName.replace(regexFind, replaceStr);
        }
        else {
            newName.replace(findStr, replaceStr, cs);
        }

        // Only attempt rename if the name actually changed
        if (!newName.isEmpty() && newName != fileName) {
            if (!Panel::changeFileName(file->path(), newName, nullptr, false)) {
                ++failed;
            }
        }

        ++i;
    }

    progress.setValue(i);

    if (failed == i && i > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("No file could be renamed."));
        return false;
    }

    if (failed > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Some files could not be renamed."));
    }

    return true;
}

bool BulkRenamer::renameByChangingCase(const Panel::FileInfoList& files,
                                       const QLocale& locale,
                                       bool toUpperCase,
                                       QWidget* parent) {
    QProgressDialog progress(QObject::tr("Renaming files..."), QObject::tr("Abort"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);

    int i = 0;
    int failed = 0;

    for (const auto& file : files) {
        progress.setValue(i);

        if (progress.wasCanceled()) {
            progress.close();
            QMessageBox::warning(parent, QObject::tr("Warning"), QObject::tr("Renaming is aborted."));
            return true;
        }

        QString fileName = effectiveFileName(file);
        QString newName = toUpperCase ? locale.toUpper(fileName) : locale.toLower(fileName);

        if (!newName.isEmpty() && newName != fileName) {
            if (!Panel::changeFileName(file->path(), newName, nullptr, false)) {
                ++failed;
            }
        }

        ++i;
    }

    progress.setValue(i);

    if (failed == i && i > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("No file could be renamed."));
        return false;
    }

    if (failed > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Some files could not be renamed."));
    }

    return true;
}

}  // namespace Oneg4FM
