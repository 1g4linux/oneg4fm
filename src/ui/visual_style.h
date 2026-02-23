/*
 * Shared visual style helpers and tokens
 * src/ui/visual_style.h
 */

#pragma once

#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QString>

namespace Oneg4FM::UiStyle {

constexpr int kSpacingXs = 4;
constexpr int kSpacingSm = 8;
constexpr int kSpacingMd = 12;
constexpr int kDialogMargin = 12;
constexpr int kWarningRadius = 4;

inline void applyDialogLayout(QLayout* layout) {
    if (!layout) {
        return;
    }
    layout->setSpacing(kSpacingSm);
    layout->setContentsMargins(kDialogMargin, kDialogMargin, kDialogMargin, kDialogMargin);
}

inline void applySectionTitle(QLabel* label) {
    if (!label) {
        return;
    }

    QFont font = label->font();
    font.setBold(true);

    const qreal pointSize = font.pointSizeF();
    if (pointSize > 0.0) {
        font.setPointSizeF(pointSize * 1.12);
    }
    else if (font.pointSize() > 0) {
        font.setPointSize(font.pointSize() + 1);
    }

    label->setFont(font);
}

inline QString warningLabelStyleSheet() {
    return QStringLiteral(
               "background-color:#b71c1c; color:white; font-weight:600; border-radius:%1px; "
               "margin:2px; padding:4px 8px;")
        .arg(kWarningRadius);
}

inline void applyWarningLabel(QLabel* label) {
    if (!label) {
        return;
    }

    label->setWordWrap(true);
    label->setStyleSheet(warningLabelStyleSheet());
}

}  // namespace Oneg4FM::UiStyle
