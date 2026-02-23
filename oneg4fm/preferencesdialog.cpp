/*
 * Preferences dialog implementation for oneg4fm
 * oneg4fm/preferencesdialog.cpp
 */

#include "preferencesdialog.h"

#include "panel/panel.h"

#include <QDir>
#include <QHash>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringBuilder>
#include <algorithm>
#include <cmath>

#include "application.h"
#include "settings.h"
#include "../src/ui/visual_style.h"

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

// Replaces the GLib GKeyFile implementation with native Qt QSettings
void findIconThemesInDir(QHash<QString, QString>& iconThemes, const QString& dirName) {
    QDir dir(dirName);
    const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);

    for (const QString& subDir : subDirs) {
        // skip hidden themes like ".something"
        if (subDir.startsWith(QLatin1Char('.'))) {
            continue;
        }

        const QString indexFilePath = dir.filePath(subDir + QStringLiteral("/index.theme"));

        // QSettings::IniFormat handles standard .theme files perfectly
        QSettings themeFile(indexFilePath, QSettings::IniFormat);

        // Icon themes must have an [Icon Theme] group with a "Directories" key
        // to distinguish them from cursor themes or other metadata.
        themeFile.beginGroup(QStringLiteral("Icon Theme"));
        if (themeFile.contains(QStringLiteral("Directories"))) {
            // QSettings automatically handles locale-based keys (e.g., Name[fr])
            QString dispName = themeFile.value(QStringLiteral("Name")).toString();
            if (!dispName.isEmpty()) {
                iconThemes.insert(subDir, dispName);
            }
        }
        themeFile.endGroup();
    }
}

}  // namespace

PreferencesDialog::PreferencesDialog(const QString& activePage, QWidget* parent) : QDialog(parent), warningCounter_(0) {
    ui.setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    UiStyle::applyWarningLabel(ui.warningLabel);
    ui.warningLabel->hide();

    // keep the page selector narrow and vertically stretchable
    ui.listWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    ui.listWidget->setMaximumWidth(ui.listWidget->sizeHintForColumn(0) + ui.listWidget->frameWidth() * 2 + 4);

    connect(ui.terminal, &QComboBox::currentIndexChanged, this, &PreferencesDialog::terminalChanged);

    ui.terminal->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.terminal->lineEdit(), &QWidget::customContextMenuRequested, this,
            &PreferencesDialog::terminalContextMenu);

    // only libfm-qt populates the terminal list, avoid adding entries by hitting Enter
    ui.terminal->setInsertPolicy(QComboBox::NoInsert);

    initFromSettings();
    selectPage(activePage);
    adjustSize();

    if (auto* applyButton = ui.buttonBox->button(QDialogButtonBox::Apply)) {
        connect(applyButton, &QPushButton::clicked, this, &PreferencesDialog::applySettings);
    }
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::initIconThemes(Settings& settings) {
    // use the fallback icon theme combo only if auto detection failed
    if (settings.useFallbackIconTheme()) {
        QHash<QString, QString> iconThemes;

        // user themes in ~/.icons and ~/.local/share/icons
        findIconThemesInDir(iconThemes, QDir::homePath() + QStringLiteral("/.icons"));

        // Qt equivalent of iterating g_get_system_data_dirs + /icons
        const auto dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        for (const auto& path : dataDirs) {
            findIconThemesInDir(iconThemes, path + QStringLiteral("/icons"));
        }

        // hicolor is a fallback and not useful as a user selectable theme here
        iconThemes.remove(QStringLiteral("hicolor"));

        for (auto it = iconThemes.cbegin(); it != iconThemes.cend(); ++it) {
            ui.iconTheme->addItem(it.value(), it.key());
        }

        ui.iconTheme->model()->sort(0);

        // select current fallback theme if present
        int index = ui.iconTheme->findData(settings.fallbackIconThemeName());
        if (index == -1)
            index = 0;  // Default to first if not found
        ui.iconTheme->setCurrentIndex(index);
    }
    else {
        // icon theme is driven by the environment, hiding the manual override
        ui.iconThemeLabel->hide();
        ui.iconTheme->hide();
    }

    ui.hMargin->setValue(settings.folderViewCellMargins().width());
    ui.vMargin->setValue(settings.folderViewCellMargins().height());
    connect(ui.lockMargins, &QAbstractButton::clicked, this, &PreferencesDialog::lockMargins);
}

void PreferencesDialog::initArchivers(Settings& settings) {
    const auto& allArchivers = Panel::Archiver::allArchivers();
    for (int i = 0; i < int(allArchivers.size()); ++i) {
        auto& archiver = allArchivers[i];
        QString program = QString::fromUtf8(archiver->program());
        ui.archiver->addItem(program, program);
        if (program == settings.archiver()) {
            ui.archiver->setCurrentIndex(i);
        }
    }
}

void PreferencesDialog::initDisplayPage(Settings& settings) {
    initIconThemes(settings);

    // Helper to populate size combos
    auto setupIconCombo = [](QComboBox* combo, int currentSize, Settings::IconType type) {
        int i = 0;
        for (int size : Settings::iconSizes(type)) {
            combo->addItem(QStringLiteral("%1 x %1").arg(size), size);
            if (currentSize == size) {
                combo->setCurrentIndex(i);
            }
            ++i;
        }
    };

    setupIconCombo(ui.bigIconSize, settings.bigIconSize(), Settings::Big);
    setupIconCombo(ui.smallIconSize, settings.smallIconSize(), Settings::Small);
    setupIconCombo(ui.sidePaneIconSize, settings.sidePaneIconSize(), Settings::Small);  // SidePane uses Small sizes
    setupIconCombo(ui.thumbnailIconSize, settings.thumbnailIconSize(), Settings::Thumbnail);

    ui.siUnit->setChecked(settings.siUnit());
    ui.backupAsHidden->setChecked(settings.backupAsHidden());
    ui.showFullNames->setChecked(settings.showFullNames());
    ui.shadowHidden->setChecked(settings.shadowHidden());
    ui.noItemTooltip->setChecked(settings.noItemTooltip());
    ui.noScrollPerPixel->setChecked(!settings.scrollPerPixel());

    // restart warning toggles for settings that affect core view behavior
    auto warningToggle = [this](bool changed) { restartWarning(changed); };

    // Note: We capture by value/reference correctly here. The previous code
    // captured 'settings' by ref, which is safe as long as Application lives longer than Dialog.

    connect(ui.showFullNames, &QAbstractButton::toggled, this,
            [warningToggle, &settings](bool checked) { warningToggle(settings.showFullNames() != checked); });

    connect(ui.shadowHidden, &QAbstractButton::toggled, this,
            [warningToggle, &settings](bool checked) { warningToggle(settings.shadowHidden() != checked); });
}

void PreferencesDialog::initUiPage(Settings& settings) {
    ui.alwaysShowTabs->setChecked(settings.alwaysShowTabs());
    ui.showTabClose->setChecked(settings.showTabClose());
    ui.switchToNewTab->setChecked(settings.switchToNewTab());
    ui.reopenLastTabs->setChecked(settings.reopenLastTabs());
    ui.rememberWindowSize->setChecked(settings.rememberWindowSize());
    ui.fixedWindowWidth->setValue(settings.fixedWindowWidth());
    ui.fixedWindowHeight->setValue(settings.fixedWindowHeight());
}

void PreferencesDialog::initBehaviorPage(Settings& settings) {
    ui.singleClick->setChecked(settings.singleClick());
    ui.autoSelectionDelay->setValue(double(settings.autoSelectionDelay()) / 1000.0);
    ui.ctrlRightClick->setChecked(settings.ctrlRightClick());

    ui.bookmarkOpenMethod->setCurrentIndex(settings.bookmarkOpenMethod());

    ui.viewMode->addItem(tr("Icon View"), int(Panel::FolderView::IconMode));
    ui.viewMode->addItem(tr("Compact View"), int(Panel::FolderView::CompactMode));
    ui.viewMode->addItem(tr("Thumbnail View"), int(Panel::FolderView::ThumbnailMode));
    ui.viewMode->addItem(tr("Detailed List View"), int(Panel::FolderView::DetailedListMode));

    int index = ui.viewMode->findData(int(settings.viewMode()));
    if (index != -1) {
        ui.viewMode->setCurrentIndex(index);
    }

    ui.configmDelete->setChecked(settings.confirmDelete());

    if (settings.supportTrash()) {
        ui.useTrash->setChecked(settings.useTrash());
    }
    else {
        ui.useTrash->hide();
    }

    ui.noUsbTrash->setChecked(settings.noUsbTrash());
    ui.confirmTrash->setChecked(settings.confirmTrash());
    ui.quickExec->setChecked(settings.quickExec());
    ui.selectNewFiles->setChecked(settings.selectNewFiles());
    ui.singleWindowMode->setChecked(settings.singleWindowMode());

    connect(ui.quickExec, &QAbstractButton::toggled, this,
            [this, &settings](bool checked) { restartWarning(settings.quickExec() != checked); });
}

void PreferencesDialog::initThumbnailPage(Settings& settings) {
    ui.showThumbnails->setChecked(settings.showThumbnails());
    ui.thumbnailLocal->setChecked(settings.thumbnailLocalFilesOnly());

    // spin boxes are in MiB, config is stored in KiB
    double m = settings.maxThumbnailFileSize();
    ui.maxThumbnailFileSize->setValue(std::clamp(m / 1024.0, 0.0, 1024.0));

    int m1 = settings.maxExternalThumbnailFileSize();
    ui.maxExternalThumbnailFileSize->setValue(m1 < 0 ? -1 : std::min(m1 / 1024, 2048));
}

void PreferencesDialog::initVolumePage(Settings& /*settings*/) {}

void PreferencesDialog::initTerminals(Settings& settings) {
    // populate terminal list from libfm-qt known terminals
    for (const auto& terminal : Panel::allKnownTerminals()) {
        ui.terminal->addItem(QString::fromUtf8(terminal.get()));
    }

    int index = ui.terminal->findText(settings.terminal());
    ui.terminal->setCurrentIndex(index);

    // if the configured terminal is not in the known list, show it as a custom value
    if (index == -1) {
        ui.terminal->setEditText(settings.terminal());
    }
}

void PreferencesDialog::terminalChanged(int index) {
    // apply saved arguments for a user defined terminal if present
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataDir.isEmpty()) {
        return;
    }

    const QString term = ui.terminal->itemText(index);
    if (term.isEmpty()) {
        ui.terminalExec->clear();
        ui.terminalCustom->clear();
        return;
    }

    QSettings termList(dataDir + QStringLiteral("/libfm-qt/terminals.list"), QSettings::IniFormat);

    if (termList.childGroups().contains(term)) {
        termList.beginGroup(term);
        ui.terminalExec->setText(termList.value(QStringLiteral("open_arg")).toString());
        ui.terminalCustom->setText(termList.value(QStringLiteral("custom_args")).toString());
        termList.endGroup();
    }
    else {
        ui.terminalExec->clear();
        ui.terminalCustom->clear();
    }
}

void PreferencesDialog::terminalContextMenu(const QPoint& p) {
    QMenu menu(this);

    if (!ui.terminal->currentText().isEmpty()) {
        QAction* rmAct = menu.addAction(tr("Remove if added by user"));
        connect(rmAct, &QAction::triggered, this, [this] {
            QString term = ui.terminal->currentText();
            auto parts = term.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                return;
            }

            const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            if (dataDir.isEmpty()) {
                return;
            }

            QSettings termList(dataDir + QStringLiteral("/libfm-qt/terminals.list"), QSettings::IniFormat);

            term = parts.at(0);
            if (termList.childGroups().contains(term)) {
                termList.remove(term);
                termList.sync();

                ui.terminal->clear();
                ui.terminal->clearEditText();

                for (const auto& terminal : Panel::allKnownTerminals()) {
                    ui.terminal->addItem(QString::fromUtf8(terminal.get()));
                }
            }
        });
    }

    QAction* openAct = menu.addAction(tr("Open user-defined list"));
    connect(openAct, &QAction::triggered, this, [] {
        const QString termList = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                                 QStringLiteral("/libfm-qt/terminals.list");
        static_cast<Application*>(qApp)->launchFiles(QDir::currentPath(), QStringList() << termList, false, false);
    });

    menu.exec(ui.terminal->lineEdit()->mapToGlobal(p));
}

void PreferencesDialog::initAdvancedPage(Settings& settings) {
    initArchivers(settings);
    initTerminals(settings);

    ui.onlyUserTemplates->setChecked(settings.onlyUserTemplates());
    ui.templateTypeOnce->setChecked(settings.templateTypeOnce());
    ui.templateRunApp->setChecked(settings.templateRunApp());
    ui.preservePermissions->setChecked(settings.preservePermissions());

    // option currently not wired to behavior, keep hidden until support is implemented
    ui.templateRunApp->hide();

    ui.maxSearchHistory->setValue(settings.maxSearchHistory());
}

void PreferencesDialog::initFromSettings() {
    Settings& settings = appSettings();

    initDisplayPage(settings);
    initUiPage(settings);
    initBehaviorPage(settings);
    initThumbnailPage(settings);
    initVolumePage(settings);
    initAdvancedPage(settings);

    connect(ui.clearSearchHistory, &QAbstractButton::clicked, this, [&settings] { settings.clearSearchHistory(); });
}

void PreferencesDialog::applyDisplayPage(Settings& settings) {
    if (settings.useFallbackIconTheme()) {
        // only apply when the combo box is visible and in use
        const QString newIconTheme = ui.iconTheme->itemData(ui.iconTheme->currentIndex()).toString();

        if (newIconTheme != settings.fallbackIconThemeName()) {
            settings.setFallbackIconThemeName(newIconTheme);
            QIcon::setThemeName(settings.fallbackIconThemeName());

            // trigger style change on all widgets to refresh icons
            const auto widgets = QApplication::allWidgets();
            for (QWidget* widget : widgets) {
                QEvent event(QEvent::StyleChange);
                QApplication::sendEvent(widget, &event);
            }
        }
    }

    settings.setBigIconSize(ui.bigIconSize->currentData().toInt());
    settings.setSmallIconSize(ui.smallIconSize->currentData().toInt());
    settings.setThumbnailIconSize(ui.thumbnailIconSize->currentData().toInt());
    settings.setSidePaneIconSize(ui.sidePaneIconSize->currentData().toInt());

    settings.setSiUnit(ui.siUnit->isChecked());
    settings.setBackupAsHidden(ui.backupAsHidden->isChecked());
    settings.setShowFullNames(ui.showFullNames->isChecked());
    settings.setShadowHidden(ui.shadowHidden->isChecked());
    settings.setNoItemTooltip(ui.noItemTooltip->isChecked());
    settings.setScrollPerPixel(!ui.noScrollPerPixel->isChecked());
    settings.setFolderViewCellMargins(QSize(ui.hMargin->value(), ui.vMargin->value()));
}

void PreferencesDialog::applyUiPage(Settings& settings) {
    settings.setAlwaysShowTabs(ui.alwaysShowTabs->isChecked());
    settings.setShowTabClose(ui.showTabClose->isChecked());
    settings.setSwitchToNewTab(ui.switchToNewTab->isChecked());
    settings.setReopenLastTabs(ui.reopenLastTabs->isChecked());
    settings.setRememberWindowSize(ui.rememberWindowSize->isChecked());
    settings.setFixedWindowWidth(ui.fixedWindowWidth->value());
    settings.setFixedWindowHeight(ui.fixedWindowHeight->value());
}

void PreferencesDialog::applyBehaviorPage(Settings& settings) {
    settings.setSingleClick(ui.singleClick->isChecked());
    settings.setAutoSelectionDelay(int(ui.autoSelectionDelay->value() * 1000));
    settings.setCtrlRightClick(ui.ctrlRightClick->isChecked());

    settings.setBookmarkOpenMethod(OpenDirTargetType(ui.bookmarkOpenMethod->currentIndex()));

    // view mode is stored in the user data of the combo box items
    Panel::FolderView::ViewMode mode = Panel::FolderView::ViewMode(ui.viewMode->currentData().toInt());
    settings.setViewMode(mode);

    settings.setConfirmDelete(ui.configmDelete->isChecked());

    if (settings.supportTrash()) {
        settings.setUseTrash(ui.useTrash->isChecked());
    }

    settings.setNoUsbTrash(ui.noUsbTrash->isChecked());
    settings.setConfirmTrash(ui.confirmTrash->isChecked());
    settings.setQuickExec(ui.quickExec->isChecked());
    settings.setSelectNewFiles(ui.selectNewFiles->isChecked());
    settings.setSingleWindowMode(ui.singleWindowMode->isChecked());
}

void PreferencesDialog::applyThumbnailPage(Settings& settings) {
    settings.setShowThumbnails(ui.showThumbnails->isChecked());
    settings.setThumbnailLocalFilesOnly(ui.thumbnailLocal->isChecked());

    // spin boxes are in MiB, config is stored in KiB
    settings.setMaxThumbnailFileSize(std::round(ui.maxThumbnailFileSize->value() * 1024));

    int m = ui.maxExternalThumbnailFileSize->value();
    settings.setMaxExternalThumbnailFileSize(m < 0 ? -1 : m * 1024);
}

void PreferencesDialog::applyVolumePage(Settings&) {}

void PreferencesDialog::applyTerminal(Settings& settings) {
    // set the terminal and, if needed, store it in the user list
    QString term = ui.terminal->currentText();
    auto parts = term.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return;
    }

    term = parts.at(0);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataDir.isEmpty()) {
        bool isGlobal = false;
        for (const auto& terminal : Panel::internalTerminals()) {
            if (term == QString::fromUtf8(terminal.get())) {
                isGlobal = true;
                break;
            }
        }

        QSettings termList(dataDir + QStringLiteral("/libfm-qt/terminals.list"), QSettings::IniFormat);

        // persist only custom terminals or custom arguments for known terminals
        if (!isGlobal || !ui.terminalExec->text().isEmpty() || !ui.terminalCustom->text().isEmpty()) {
            termList.beginGroup(term);
            termList.setValue(QStringLiteral("open_arg"), ui.terminalExec->text());
            termList.setValue(QStringLiteral("custom_args"), ui.terminalCustom->text());
            termList.endGroup();
        }
        else {
            if (termList.childGroups().contains(term)) {
                termList.remove(term);
            }
        }
    }

    settings.setTerminal(term);
}

void PreferencesDialog::applyAdvancedPage(Settings& settings) {
    applyTerminal(settings);

    settings.setArchiver(ui.archiver->currentData().toString());
    settings.setPreservePermissions(ui.preservePermissions->isChecked());

    settings.setOnlyUserTemplates(ui.onlyUserTemplates->isChecked());
    settings.setTemplateTypeOnce(ui.templateTypeOnce->isChecked());
    settings.setTemplateRunApp(ui.templateRunApp->isChecked());
    settings.setMaxSearchHistory(ui.maxSearchHistory->value());
}

void PreferencesDialog::applySettings() {
    Settings& settings = appSettings();

    applyDisplayPage(settings);
    applyUiPage(settings);
    applyBehaviorPage(settings);
    applyThumbnailPage(settings);
    applyVolumePage(settings);
    applyAdvancedPage(settings);

    settings.save();

    static_cast<Application*>(qApp)->updateFromSettings();
}

void PreferencesDialog::accept() {
    applySettings();
    QDialog::accept();
}

void PreferencesDialog::selectPage(const QString& name) {
    if (name.isEmpty()) {
        return;
    }

    QWidget* page = findChild<QWidget*>(name + QStringLiteral("Page"));
    if (!page) {
        return;
    }

    const int index = ui.stackedWidget->indexOf(page);
    if (index >= 0) {
        ui.listWidget->setCurrentRow(index);
    }
}

void PreferencesDialog::lockMargins(bool lock) {
    ui.vMargin->setDisabled(lock);
    if (lock) {
        ui.vMargin->setValue(ui.hMargin->value());
        connect(ui.hMargin, &QSpinBox::valueChanged, ui.vMargin, &QSpinBox::setValue);
    }
    else {
        disconnect(ui.hMargin, &QSpinBox::valueChanged, ui.vMargin, &QSpinBox::setValue);
    }
}

void PreferencesDialog::restartWarning(bool warn) {
    if (warn) {
        ++warningCounter_;
    }
    else {
        --warningCounter_;
    }
    ui.warningLabel->setVisible(warningCounter_ > 0);
}

}  // namespace Oneg4FM
