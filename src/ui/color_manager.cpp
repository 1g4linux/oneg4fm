/*
 * Color scheme manager for hex/disassembly views
 * src/ui/color_manager.cpp
 */

#include "color_manager.h"

#include <QApplication>
#include <QPalette>

namespace Oneg4FM {

namespace {
QColor loadColor(QSettings& settings, const QString& key, const QColor& fallback) {
    const QVariant v = settings.value(key);
    if (!v.isValid()) {
        return fallback;
    }
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}
}  // namespace

ColorManager::ColorManager(QObject* parent) : QObject(parent) {
    QPalette pal = QApplication::palette();
    scheme_.background = pal.base().color();
    scheme_.foreground = pal.text().color();
    scheme_.address = pal.mid().color();
    scheme_.bytes = scheme_.foreground;
    scheme_.mnemonic = QColor(200, 200, 255);
    scheme_.operands = scheme_.foreground;
    scheme_.branch = QColor(255, 180, 180);
    scheme_.call = QColor(180, 255, 180);
    scheme_.ret = QColor(255, 210, 120);
    scheme_.nop = QColor(180, 180, 180);
    scheme_.searchHitBg = QColor(0, 255, 0, 50);
    scheme_.bookmarkBg = QColor(255, 255, 0, 60);
    scheme_.patchedBg = QColor(255, 200, 0, 40);

    loadFromSettings();
}

void ColorManager::loadFromSettings() {
    QSettings settings;
    scheme_.background = loadColor(settings, QStringLiteral("colors/background"), scheme_.background);
    scheme_.foreground = loadColor(settings, QStringLiteral("colors/foreground"), scheme_.foreground);
    scheme_.address = loadColor(settings, QStringLiteral("colors/address"), scheme_.address);
    scheme_.bytes = loadColor(settings, QStringLiteral("colors/bytes"), scheme_.bytes);
    scheme_.mnemonic = loadColor(settings, QStringLiteral("colors/mnemonic"), scheme_.mnemonic);
    scheme_.operands = loadColor(settings, QStringLiteral("colors/operands"), scheme_.operands);
    scheme_.branch = loadColor(settings, QStringLiteral("colors/branch"), scheme_.branch);
    scheme_.call = loadColor(settings, QStringLiteral("colors/call"), scheme_.call);
    scheme_.ret = loadColor(settings, QStringLiteral("colors/ret"), scheme_.ret);
    scheme_.nop = loadColor(settings, QStringLiteral("colors/nop"), scheme_.nop);
    scheme_.searchHitBg = loadColor(settings, QStringLiteral("colors/searchHitBg"), scheme_.searchHitBg);
    scheme_.bookmarkBg = loadColor(settings, QStringLiteral("colors/bookmarkBg"), scheme_.bookmarkBg);
    scheme_.patchedBg = loadColor(settings, QStringLiteral("colors/patchedBg"), scheme_.patchedBg);
}

void ColorManager::saveToSettings() const {
    QSettings settings;
    settings.setValue(QStringLiteral("colors/background"), scheme_.background.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/foreground"), scheme_.foreground.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/address"), scheme_.address.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/bytes"), scheme_.bytes.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/mnemonic"), scheme_.mnemonic.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/operands"), scheme_.operands.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/branch"), scheme_.branch.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/call"), scheme_.call.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/ret"), scheme_.ret.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/nop"), scheme_.nop.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/searchHitBg"), scheme_.searchHitBg.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/bookmarkBg"), scheme_.bookmarkBg.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("colors/patchedBg"), scheme_.patchedBg.name(QColor::HexArgb));
}

void ColorManager::setScheme(const ColorScheme& s) {
    scheme_ = s;
    Q_EMIT schemeChanged();
}

}  // namespace Oneg4FM
