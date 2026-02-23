/*
 * Color scheme manager for hex/disassembly views
 * src/ui/color_manager.h
 */

#ifndef PCMANFM_COLOR_MANAGER_H
#define PCMANFM_COLOR_MANAGER_H

#include <QColor>
#include <QObject>
#include <QSettings>

#include "color_roles.h"

namespace Oneg4FM {

struct ColorScheme {
    QColor background;
    QColor foreground;
    QColor address;
    QColor bytes;
    QColor mnemonic;
    QColor operands;
    QColor branch;
    QColor call;
    QColor ret;
    QColor nop;
    QColor searchHitBg;
    QColor bookmarkBg;
    QColor patchedBg;
};

class ColorManager : public QObject {
    Q_OBJECT
   public:
    explicit ColorManager(QObject* parent = nullptr);

    const ColorScheme& scheme() const { return scheme_; }

    void loadFromSettings();
    void saveToSettings() const;
    void setScheme(const ColorScheme& s);

   Q_SIGNALS:
    void schemeChanged();

   private:
    ColorScheme scheme_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_COLOR_MANAGER_H
